// shelly_pro3em.cpp — Passerelle MQTT -> Shelly Pro 3EM virtuel (pour S-Miles / Hoymiles)
//
// Réécriture C++ minimale du principe de uni-meter (https://github.com/sdeigm/uni-meter,
// sortie « shelly-pro3em ») : mêmes routes, mêmes formats JSON, même annonce mDNS.
//
// Dépendances (Arch) : pacman -S paho-mqtt-c cpp-httplib nlohmann-json avahi
// cpp-httplib >= 0.33 requis (WebSocket). Si la version du paquet est plus ancienne, placer
// l'en-tête à côté du source :
//   curl -LO https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.63.0/httplib.h
// Compilation       : g++ -std=c++17 -O2 -I. -o shelly_pro3em shelly_pro3em.cpp -lpaho-mqtt3as -pthread
//
// Convention de signe (celle d'un compteur réseau Shelly) :
//   puissance > 0 : on SOUTIRE du réseau (consommation)
//   puissance < 0 : on INJECTE dans le réseau (surplus)
//
// Configuration par variables d'environnement :
//   MQTT_URI             tcp://localhost:1883
//   MQTT_USER / MQTT_PASSWORD           (optionnels)
//   TOPIC_PREFIX         TeleInfo/LinkyConsommation   (topics lus : <prefix>/<étiquette>)
//   POWER_MODE           net | import   (net = SINSTS - SINSTI, signé ; import = SINSTS seul)
//   IFACE                interface réseau (défaut : première interface IPv4 non loopback)
//   SHELLY_MAC           forcer la MAC (12 hex)       (défaut : MAC réelle de IFACE)
//   SHELLY_HOSTNAME      forcer le nom                (défaut : ShellyPro3EM-<MAC>)
//   HTTP_PORT            80   (Hoymiles l'impose)
//   UDP_RPC_PORT         0    (0 = désactivé ; 1010 pour une batterie Marstek)
//   VOLTAGE              230  (utilisée seulement si ni URMS1 ni UMOY1 n'ont été reçus)
//   MDNS                 avahi | none   (défaut avahi : écrit /etc/avahi/services + lance avahi-publish,
//                        relancé au bout de 60 s s'il s'arrête ; fichiers supprimés à l'arrêt)
//   VERBOSE              1 pour journaliser chaque requête avec sa réponse
//   TCP_NODELAY          0  (1 = désactiver Nagle ; à n'activer que pour tester)
//
// Correspondance téléinfo Linky monophasé (mode standard) -> Shelly Pro 3EM (tout sur la phase A) :
//   SINSTS - SINSTI      -> a_act_power / total_act_power (W ; VA assimilés à des W)
//   |puissance|          -> a_aprt_power / total_aprt_power (VA)
//   URMS1 (sinon UMOY1)  -> a_voltage (V)          IRMS1 -> a_current / total_current (A)
//   EAST                 -> EMData a_total_act_energy / total_act (Wh)
//   EAIT                 -> EMData a_total_act_ret_energy / total_act_ret (Wh)
//   NTARF, LTARF, EASF01, EASF02 : pas d'équivalent dans l'API Shelly, non utilisés

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "MQTTAsync.h"
#include "httplib.h"
#ifndef CPPHTTPLIB_WEBSOCKET_CLOSE_TIMEOUT_SECOND
#error "cpp-httplib >= 0.33 requis (support WebSocket) : voir l'en-tête de ce fichier"
#endif
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

// ===========================================================================
// Configuration
// ===========================================================================
static std::string env_or(const char *name, const std::string &def) {
    const char *v = std::getenv(name);
    return (v && *v) ? v : def;
}

static std::string upper(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::toupper); return s; }
static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

// Première interface IPv4 non loopback (ou celle demandée) -> nom + IP
static bool detect_interface(std::string &iface, std::string &ip) {
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return false;
    bool found = false;
    for (ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (a->ifa_flags & IFF_LOOPBACK) continue;
        if (!iface.empty() && iface != a->ifa_name) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(a->ifa_addr)->sin_addr, buf, sizeof buf);
        iface = a->ifa_name;
        ip = buf;
        found = true;
        break;
    }
    freeifaddrs(list);
    return found;
}

static std::string read_mac(const std::string &iface) {
    std::ifstream f("/sys/class/net/" + iface + "/address");
    std::string mac;
    std::getline(f, mac);
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return upper(mac);
}

struct Config {
    std::string mqtt_uri, mqtt_user, mqtt_password;
    std::string topic_prefix;
    bool power_net = true;
    std::string iface, ip, mac, hostname;
    int http_port = 80, udp_port = 0;
    double voltage = 230.0;
    bool mdns_avahi = true, verbose = false;

    std::string mac_colon() const {
        std::string r;
        for (size_t i = 0; i < mac.size(); i += 2) { if (i) r += ':'; r += mac.substr(i, 2); }
        return r;
    }
};
static Config cfg;

static void load_config() {
    cfg.mqtt_uri            = env_or("MQTT_URI", "tcp://localhost:1883");
    cfg.mqtt_user           = env_or("MQTT_USER", "");
    cfg.mqtt_password       = env_or("MQTT_PASSWORD", "");
    cfg.topic_prefix        = env_or("TOPIC_PREFIX", "TeleInfo/LinkyConsommation");
    while (!cfg.topic_prefix.empty() && cfg.topic_prefix.back() == '/') cfg.topic_prefix.pop_back();
    cfg.power_net           = env_or("POWER_MODE", "net") != "import";
    cfg.http_port           = std::stoi(env_or("HTTP_PORT", "80"));
    cfg.udp_port            = std::stoi(env_or("UDP_RPC_PORT", "0"));
    cfg.voltage             = std::stod(env_or("VOLTAGE", "230"));
    cfg.mdns_avahi          = env_or("MDNS", "avahi") == "avahi";
    cfg.verbose             = env_or("VERBOSE", "0") == "1";

    cfg.iface = env_or("IFACE", "");
    if (!detect_interface(cfg.iface, cfg.ip))
        throw std::runtime_error("aucune interface IPv4 trouvée" + (cfg.iface.empty() ? "" : " (" + cfg.iface + ")"));

    cfg.mac = upper(env_or("SHELLY_MAC", read_mac(cfg.iface)));
    if (cfg.mac.size() != 12) throw std::runtime_error("MAC invalide : '" + cfg.mac + "'");
    cfg.hostname = env_or("SHELLY_HOSTNAME", "ShellyPro3EM-" + cfg.mac);

}

// Valeurs reprises de la configuration par défaut de uni-meter
static const char *MODEL       = "SPEM-003CEBEU";
static const char *FW_ID       = "20250924-062729/1.7.1-gd336f31";
static const char *DEVINFO_VER = "1.4.4";
static const char *MDNS_GEN    = "3";
static const char *MDNS_APP    = "shellypro3em";
static const char *MDNS_VER    = "1.7.1";

// ===========================================================================
// État alimenté par MQTT
// ===========================================================================
// Étiquettes téléinfo utilisées (topic = <TOPIC_PREFIX>/<étiquette>)
static const char *const LABELS[] = {"SINSTS", "SINSTI", "URMS1", "UMOY1", "IRMS1", "EAST", "EAIT"};

struct State {
    std::mutex mtx;
    std::unordered_map<std::string, double> v;   // dernière valeur reçue par étiquette
};
static State g_state;
static const std::time_t g_boot = std::time(nullptr);

struct RpcError : std::runtime_error {
    int code;
    RpcError(int c, const std::string &m) : std::runtime_error(m), code(c) {}
};

static double r2(double v) { return std::round(v * 100.0) / 100.0; }

// Instantané cohérent des mesures (dernières valeurs MQTT reçues, 0 au départ)
struct Measures { double power = 0, voltage = 0, current = 0, east = 0, eait = 0; };

static Measures snapshot() {
    std::lock_guard<std::mutex> lock(g_state.mtx);
    auto get = [](const char *k, double def) {
        auto it = g_state.v.find(k);
        return it == g_state.v.end() ? def : it->second;
    };
    Measures m;
    double sinsts = get("SINSTS", 0.0), sinsti = get("SINSTI", 0.0);
    m.power   = cfg.power_net ? sinsts - sinsti : sinsts;   // > 0 soutirage, < 0 injection
    m.voltage = get("URMS1", get("UMOY1", cfg.voltage));
    double irms = get("IRMS1", -1.0);
    m.current = irms >= 0 ? irms : (m.voltage > 0 ? std::fabs(m.power) / m.voltage : 0.0);
    m.east    = get("EAST", 0.0);
    m.eait    = get("EAIT", 0.0);
    return m;
}

// ===========================================================================
// Réponses Shelly Pro 3EM
// ===========================================================================
static json device_info() {
    return {
        {"name", nullptr}, {"id", cfg.hostname}, {"mac", cfg.mac}, {"slot", 1},
        {"model", MODEL}, {"gen", 2}, {"fw_id", FW_ID}, {"ver", DEVINFO_VER},
        {"app", "Pro3EM"}, {"auth_en", false}, {"auth_domain", nullptr}, {"profile", "triphase"}
    };
}

// Monophasé : toute la puissance sur la phase A, phases B et C à zéro
static json em_status() {
    Measures m = snapshot();
    double p = m.power, v = m.voltage, i = m.current;
    auto phase = [&](const char *ph, double pw, double cur) {
        std::string s(ph);
        return json{
            {s + "_current", r2(cur)}, {s + "_voltage", r2(v)}, {s + "_act_power", r2(pw)},
            {s + "_aprt_power", r2(std::fabs(pw))}, {s + "_pf", 1.0}, {s + "_freq", 50.0}
        };
    };
    json j = {{"id", 0}};
    j.update(phase("a", p, i));
    j.update(phase("b", 0.0, 0.0));
    j.update(phase("c", 0.0, 0.0));
    j["n_current"] = nullptr;
    j["total_current"] = r2(i);
    j["total_act_power"] = r2(p);
    j["total_aprt_power"] = r2(std::fabs(p));
    j["user_calibrated_phase"] = json::array();
    return j;
}

static json emdata_status() {
    Measures m = snapshot();
    double imp = m.east, exp = m.eait;
    return {
        {"id", 0},
        {"a_total_act_energy", r2(imp)}, {"a_total_act_ret_energy", r2(exp)},
        {"b_total_act_energy", 0.0},     {"b_total_act_ret_energy", 0.0},
        {"c_total_act_energy", 0.0},     {"c_total_act_ret_energy", 0.0},
        {"total_act", r2(imp)},          {"total_act_ret", r2(exp)}
    };
}

static json em_config() {
    return {
        {"id", 0}, {"name", nullptr}, {"blink_mode_selector", "active_energy"},
        {"phase_selector", "a"}, {"monitor_phase_sequence", true},
        {"reverse", json::object()}, {"ct_type", "120A"}
    };
}

static int utc_offset() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    return static_cast<int>(tm.tm_gmtoff);
}

static json sys_status() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char hm[6];
    std::strftime(hm, sizeof hm, "%H:%M", &tm);
    return {
        {"mac", cfg.mac}, {"restart_required", false}, {"time", std::string(hm)},
        {"unixtime", now}, {"last_sync_ts", now}, {"uptime", now - g_boot},
        {"ram_size", 247488}, {"ram_free", 113000}, {"ram_min_free", 98000},
        {"fs_size", 524288}, {"fs_free", 196608},
        {"cfg_rev", 9}, {"kvs_rev", 0}, {"schedule_rev", 0}, {"webhook_rev", 0}, {"btrelay_ref", 0},
        {"available_updates", {{"beta", {{"version", "1.7.5-beta1"}}}}},
        {"reset_reason", 1}, {"utc_offset", utc_offset()}
    };
}

static json sys_config() {
    return {
        {"device", {{"name", cfg.hostname}, {"mac", cfg.mac}, {"fw_id", FW_ID},
                    {"eco_mode", false}, {"profile", ""}, {"discoverable", false}}},
        {"location", {{"tz", "Europe/Paris"}, {"lat", 48.85}, {"lon", 2.35}}},
        {"debug", {{"mqtt", {{"enable", false}}}, {"websocket", {{"enable", false}}},
                   {"udp", {{"addr", nullptr}}}}},
        {"ui_data", json::object()},
        {"rpc_udp", {{"dst_addr", nullptr},
                     {"listen_port", cfg.udp_port > 0 ? json(cfg.udp_port) : json(nullptr)}}},
        {"sntp", {{"server", "pool.ntp.org"}}},
        {"cfg_rev", 10}
    };
}

static json wifi_status() {
    return {{"sta_ip", cfg.ip}, {"status", "got ip"}, {"ssid", "wifi-ssid"},
            {"bssid", "04:b4:fe:a6:ec:38"}, {"rssi", -60}, {"sta_ip6", json::array()}};
}

static json wifi_config() {
    json sta = {{"ssid", "wifi-ssid"}, {"is_open", false}, {"enable", true}, {"ipv4mode", "dhcp"},
                {"ip", nullptr}, {"netmask", nullptr}, {"gw", nullptr}, {"nameserver", nullptr}};
    json sta1 = sta;
    sta1["ssid"] = nullptr; sta1["is_open"] = true; sta1["enable"] = false;
    return {
        {"ap", {{"ssid", cfg.hostname}, {"is_open", true}, {"enable", false}, {"range_extender", {{"enable", false}}}}},
        {"sta", sta}, {"sta1", sta1}, {"roam", {{"rssi_thr", -80}, {"interval", 60}}}
    };
}

static json temperature_status() { return {{"id", 0}, {"tC", 39.1}, {"tF", 102.4}}; }
static json temperature_config() { return {{"id", 0}, {"method", nullptr}, {"report_thr_C", 5.0}, {"offset_C", 0.0}}; }
static json cloud_status()       { return {{"enabled", false}, {"connected", false}}; }
static json cloud_config()       { return {{"enable", false}, {"server", "shelly-143-eu.shelly.cloud:6022/jrpc"}}; }
static json ws_config()          { return {{"enable", false}, {"server", nullptr}, {"ssl_ca", "*"}}; }
static json ble_config()         { return {{"enable", false}, {"rpc", {{"enable", true}}}, {"observer", {{"enable", false}}}}; }

static json shelly_status() {
    return {
        {"ble", json::object()}, {"bthome", json::object()}, {"cloud", cloud_status()},
        {"em:0", em_status()}, {"emdata:0", emdata_status()},
        {"eth", {{"ip", nullptr}, {"ip6", nullptr}}}, {"modbus", json::object()},
        {"mqtt", {{"connected", false}}}, {"sys", sys_status()},
        {"temperature:0", temperature_status()}, {"wifi", wifi_status()}, {"ws", {{"connected", false}}}
    };
}

static json shelly_config() {
    return {
        {"ble", ble_config()}, {"cloud", cloud_config()}, {"em:0", em_config()},
        {"sys", sys_config()}, {"temperature:0", temperature_config()},
        {"wifi", wifi_config()}, {"ws", ws_config()}
    };
}

static json shelly_components() {
    return {
        {"components", json::array({
            {{"key", "ble"},      {"status", json::object()}, {"config", ble_config()}},
            {{"key", "em:0"},     {"status", em_status()},    {"config", em_config()}},
            {{"key", "emdata:0"}, {"status", emdata_status()}, {"config", json::object()}}
        })},
        {"cfg_rev", 1}, {"offset", 0}, {"total", 2}
    };
}

// Réponse Gen1 /settings (certains clients l'interrogent)
static json gen1_settings() {
    return {
        {"device", {{"type", "SPEM-003CEBEU120"}, {"mac", cfg.mac}, {"hostname", cfg.hostname},
                    {"num_outputs", 0}, {"num_meters", 3}}},
        {"login", {{"enabled", false}, {"unprotected", false}, {"username", "admin"}}},
        {"fw", FW_ID}, {"discoverable", true}
    };
}

// ===========================================================================
// Dispatch RPC (noms insensibles à la casse)
// ===========================================================================
using RpcHandler = std::function<json(const json &)>;

static const std::unordered_map<std::string, RpcHandler> &rpc_table() {
    static const std::unordered_map<std::string, RpcHandler> t = {
        {"shelly.getdeviceinfo",  [](const json &) { return device_info(); }},
        {"shelly.getstatus",      [](const json &) { return shelly_status(); }},
        {"shelly.getconfig",      [](const json &) { return shelly_config(); }},
        {"shelly.getcomponents",  [](const json &) { return shelly_components(); }},
        {"shelly.reboot",         [](const json &) { return json::object(); }},
        {"em.getstatus",          [](const json &) { return em_status(); }},
        {"em.getconfig",          [](const json &) { return em_config(); }},
        {"emdata.getstatus",      [](const json &) { return emdata_status(); }},
        {"emdata.getconfig",      [](const json &) { return json::object(); }},
        {"sys.getstatus",         [](const json &) { return sys_status(); }},
        {"sys.getconfig",         [](const json &) { return sys_config(); }},
        {"wifi.getstatus",        [](const json &) { return wifi_status(); }},
        {"wifi.getconfig",        [](const json &) { return wifi_config(); }},
        {"cloud.getstatus",       [](const json &) { return cloud_status(); }},
        {"cloud.getconfig",       [](const json &) { return cloud_config(); }},
        {"cloud.setconfig",       [](const json &) { return json{{"restart_required", false}}; }},
        {"ws.getconfig",          [](const json &) { return ws_config(); }},
        {"ws.setconfig",          [](const json &) { return json{{"restart_required", true}}; }},
        {"mqtt.getstatus",        [](const json &) { return json{{"connected", false}}; }},
        {"temperature.getstatus", [](const json &) { return temperature_status(); }},
        {"temperature.getconfig", [](const json &) { return temperature_config(); }},
        {"script.list",           [](const json &) { return json{{"scripts", json::array()}}; }},
        {"script.getcode",        [](const json &) { return json{{"data", ""}, {"left", 0}}; }},
    };
    return t;
}

// Lève RpcError si méthode inconnue ou pas de donnée
static json rpc_call(const std::string &method, const json &params) {
    auto it = rpc_table().find(lower(method));
    if (it == rpc_table().end()) throw RpcError(404, "No handler for " + method);
    return it->second(params);
}

// Requête JSON-RPC complète -> trame de réponse {id, src, dst, result|error}
static json rpc_frame(const json &req) {
    json resp;
    resp["id"] = req.contains("id") ? req["id"] : json(0);
    resp["src"] = cfg.hostname;
    if (req.contains("src")) resp["dst"] = req["src"];
    const std::string method = req.value("method", "");
    const json params = (req.contains("params") && req["params"].is_object()) ? req["params"] : json::object();
    try {
        resp["result"] = rpc_call(method, params);
    } catch (const RpcError &e) {
        resp["error"] = {{"code", e.code}, {"message", e.what()}};
    }
    return resp;
}

// ===========================================================================
// Journalisation (première occurrence par client+requête, ou tout si VERBOSE=1)
// ===========================================================================
static std::mutex g_log_mtx;
static std::set<std::string> g_seen;

static bool should_log(const std::string &key, bool is_error) {
    if (cfg.verbose || is_error) return true;
    std::lock_guard<std::mutex> lock(g_log_mtx);
    return g_seen.insert(key).second;
}

static std::string trunc(const std::string &s, size_t n = 400) {
    return s.size() > n ? s.substr(0, n) + "…" : s;
}

// ===========================================================================
// UDP JSON-RPC (Marstek & co)
// ===========================================================================
static void udp_server(int port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (s < 0 || bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        std::cerr << "[UDP] Impossible d'écouter sur le port " << port << std::endl;
        return;
    }
    std::cout << "[UDP] JSON-RPC en écoute sur le port " << port << std::endl;
    char buf[4096];
    for (;;) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &fl);
        if (n <= 0) continue;
        std::string msg(buf, static_cast<size_t>(n));
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
        json req = json::parse(msg, nullptr, false);
        if (req.is_discarded() || !req.is_object()) continue;
        json resp = rpc_frame(req);
        std::string out = resp.dump();
        sendto(s, out.data(), out.size(), 0, reinterpret_cast<sockaddr *>(&from), fl);
        std::string who = inet_ntoa(from.sin_addr);
        if (should_log("udp " + who + " " + req.value("method", ""), resp.contains("error")))
            std::cout << "[UDP] " << who << " <- " << trunc(msg) << "\n      -> " << trunc(out) << std::endl;
    }
}

// ===========================================================================
// MQTT
// ===========================================================================
static MQTTAsync g_client;
static MQTTAsync_connectOptions g_conn = MQTTAsync_connectOptions_initializer;

static void on_connected(void *, char *) {
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    for (const char *label : LABELS) {
        const std::string t = cfg.topic_prefix + "/" + label;
        int rc = MQTTAsync_subscribe(g_client, t.c_str(), 0, &opts);
        std::cout << "[MQTT] Abonnement " << t << (rc == MQTTASYNC_SUCCESS ? " : OK" : " : ÉCHEC") << std::endl;
    }
}

static void on_connect_failure(void *, MQTTAsync_failureData *r) {
    std::cerr << "[MQTT] Échec de connexion (code " << (r ? r->code : 0) << "), nouvel essai dans 5 s" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    MQTTAsync_connect(g_client, &g_conn);
}

static void on_connection_lost(void *, char *cause) {
    std::cerr << "[MQTT] Connexion perdue : " << (cause ? cause : "inconnue") << std::endl;
}

static int on_message(void *, char *topic_c, int, MQTTAsync_message *m) {
    std::string topic(topic_c ? topic_c : "");
    std::string payload(static_cast<char *>(m->payload), m->payloadlen);
    MQTTAsync_freeMessage(&m);
    MQTTAsync_free(topic_c);

    double v;
    try { v = std::stod(payload); }
    catch (...) { std::cerr << "[MQTT] Valeur non numérique sur " << topic << " : " << payload << std::endl; return 1; }

    const std::string prefix = cfg.topic_prefix + "/";
    if (topic.compare(0, prefix.size(), prefix) != 0) return 1;
    const std::string label = topic.substr(prefix.size());

    static std::set<std::string> first;
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.v[label] = v;
    if (first.insert(label).second) std::cout << "[MQTT] Première valeur " << label << " = " << v << std::endl;
    return 1;
}

// ===========================================================================
// mDNS via avahi (même schéma que uni-meter)
// ===========================================================================
static std::vector<std::string> g_avahi_files;
static std::atomic<bool> g_stopping{false};
static std::atomic<pid_t> g_publish_pid{-1};

static void avahi_write_services() {
    const std::string dir = "/etc/avahi/services";
    for (const char *type : {"_shelly", "_http"}) {
        std::string file = dir + "/" + cfg.hostname + type + ".service";
        std::ofstream f(file);
        if (!f) { std::cerr << "[mDNS] Impossible d'écrire " << file << " (root ? avahi installé ?)" << std::endl; continue; }
        f << "<?xml version=\"1.0\" standalone='no'?>\n"
          << "<!DOCTYPE service-group SYSTEM \"avahi-service.dtd\">\n"
          << "<!-- Généré automatiquement par shelly_pro3em, supprimé à son arrêt -->\n"
          << "<service-group>\n"
          << "  <name replace-wildcards=\"no\">" << cfg.hostname << "</name>\n"
          << "  <service protocol=\"ipv4\">\n"
          << "    <host-name>" << cfg.hostname << ".local</host-name>\n"
          << "    <type>" << type << "._tcp</type>\n"
          << "    <port>" << cfg.http_port << "</port>\n"
          << "    <txt-record>id=" << lower(cfg.hostname) << "</txt-record>\n"
          << "    <txt-record>mac=" << cfg.mac_colon() << "</txt-record>\n"
          << "    <txt-record>gen=" << MDNS_GEN << "</txt-record>\n"
          << "    <txt-record>app=" << MDNS_APP << "</txt-record>\n"
          << "    <txt-record>model=" << MODEL << "</txt-record>\n"
          << "    <txt-record>arch=esp32</txt-record>\n"
          << "    <txt-record>fw_id=" << FW_ID << "</txt-record>\n"
          << "    <txt-record>ver=" << MDNS_VER << "</txt-record>\n"
          << "  </service>\n"
          << "</service-group>\n";
        g_avahi_files.push_back(file);
        std::cout << "[mDNS] Service écrit : " << file << std::endl;
    }
}

// Publie l'enregistrement A <hostname>.local -> IP ; relance avahi-publish s'il s'arrête
static void avahi_publish_loop() {
    const std::string host = cfg.hostname + ".local";
    while (!g_stopping) {
        pid_t pid = fork();
        if (pid == 0) {
            sigset_t all;
            sigfillset(&all);
            sigprocmask(SIG_UNBLOCK, &all, nullptr);  // masque hérité du parent : sinon SIGTERM ignoré
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            execlp("avahi-publish", "avahi-publish", "-a", "-R", host.c_str(), cfg.ip.c_str(), (char *)nullptr);
            _exit(127);
        }
        if (pid > 0) {
            g_publish_pid = pid;
            std::cout << "[mDNS] avahi-publish -a -R " << host << " " << cfg.ip << " (pid " << pid << ")" << std::endl;
            int status = 0;
            waitpid(pid, &status, 0);
            g_publish_pid = -1;
            if (g_stopping) break;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
                std::cerr << "[mDNS] avahi-publish introuvable (paquet avahi installé ?)" << std::endl;
            else
                std::cerr << "[mDNS] avahi-publish s'est arrêté (statut " << status << ")" << std::endl;
        } else {
            std::cerr << "[mDNS] fork impossible : " << std::strerror(errno) << std::endl;
        }
        std::cerr << "[mDNS] Nouvelle tentative dans 60 s" << std::endl;
        for (int i = 0; i < 60 && !g_stopping; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

static void avahi_unregister() {
    g_stopping = true;
    for (const auto &f : g_avahi_files) std::remove(f.c_str());
    pid_t pid = g_publish_pid;
    if (pid > 0) kill(pid, SIGTERM);
}


// ===========================================================================
// Envoi d'une trame WebSocket en UN seul segment TCP
// ---------------------------------------------------------------------------
// cpp-httplib écrit l'en-tête de trame (2 octets), la longueur étendue puis la charge
// en plusieurs write(). L'onduleur Hoymiles ne supporte pas une trame découpée sur
// plusieurs segments : il lit l'en-tête seul, abandonne et ferme la connexion
// (vu en capture : FIN qui n'acquitte que les 2 premiers octets).
// On retrouve donc la socket de la connexion et on l'entoure de TCP_CORK pendant
// l'envoi : le noyau accumule tout et l'émet d'un bloc au « décorkage ».
// ===========================================================================
static int find_peer_socket(const std::string &peer_ip, int peer_port) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int found = -1;
    while (dirent *e = readdir(d)) {
        char *end = nullptr;
        long fd = std::strtol(e->d_name, &end, 10);
        if (!end || *end != '\0') continue;
        sockaddr_storage ss{};
        socklen_t len = sizeof ss;
        if (getpeername(static_cast<int>(fd), reinterpret_cast<sockaddr *>(&ss), &len) != 0) continue;
        char buf[INET6_ADDRSTRLEN] = {0};
        int port = -1;
        if (ss.ss_family == AF_INET) {
            auto *a = reinterpret_cast<sockaddr_in *>(&ss);
            inet_ntop(AF_INET, &a->sin_addr, buf, sizeof buf);
            port = ntohs(a->sin_port);
        } else if (ss.ss_family == AF_INET6) {
            auto *a = reinterpret_cast<sockaddr_in6 *>(&ss);
            inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof buf);
            port = ntohs(a->sin6_port);
        } else {
            continue;
        }
        std::string ip(buf);
        if (ip.rfind("::ffff:", 0) == 0) ip = ip.substr(7);
        if (port == peer_port && ip == peer_ip) { found = static_cast<int>(fd); break; }
    }
    closedir(d);
    return found;
}

static void set_cork(int fd, bool on) {
    if (fd < 0) return;
    int v = on ? 1 : 0;
    setsockopt(fd, IPPROTO_TCP, TCP_CORK, &v, sizeof v);
}

static std::atomic<bool> g_listen_done{false};

// ===========================================================================
// main
// ===========================================================================
int main() {
    std::cout.setf(std::ios::unitbuf);

    try { load_config(); }
    catch (const std::exception &e) { std::cerr << "[INIT] " << e.what() << std::endl; return 1; }

    std::cout << "[INIT] " << cfg.hostname << " (" << MODEL << ") sur " << cfg.iface << " " << cfg.ip
              << ", MAC " << cfg.mac_colon() << std::endl;

    // Bloque SIGINT/SIGTERM dans tous les threads ; un thread dédié les attend
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

    // --- MQTT
    if (MQTTAsync_create(&g_client, cfg.mqtt_uri.c_str(), "ShellyPro3EMProxy",
                         MQTTCLIENT_PERSISTENCE_NONE, nullptr) != MQTTASYNC_SUCCESS) {
        std::cerr << "[MQTT] Échec de création du client" << std::endl;
        return 1;
    }
    MQTTAsync_setCallbacks(g_client, nullptr, on_connection_lost, on_message, nullptr);
    MQTTAsync_setConnected(g_client, nullptr, on_connected);
    g_conn.keepAliveInterval  = 20;
    g_conn.cleansession       = 1;
    g_conn.automaticReconnect = 1;
    g_conn.minRetryInterval   = 1;
    g_conn.maxRetryInterval   = 30;
    g_conn.onFailure          = on_connect_failure;
    if (!cfg.mqtt_user.empty()) {
        g_conn.username = cfg.mqtt_user.c_str();
        g_conn.password = cfg.mqtt_password.c_str();
    }
    MQTTAsync_connect(g_client, &g_conn);

    // --- UDP
    if (cfg.udp_port > 0) std::thread(udp_server, cfg.udp_port).detach();

    // --- HTTP
    httplib::Server svr;

    // cpp-httplib active SO_REUSEPORT par défaut : deux instances pourraient alors partager
    // le port 80 sans erreur, le noyau répartissant les connexions entre elles.
    // On ne garde que SO_REUSEADDR : une 2e instance échouera clairement au démarrage.
    // TCP_NODELAY (Nagle désactivé) : OFF par défaut. Avec Nagle, l'en-tête d'une trame WebSocket part
    // seul et le reste suit à l'acquittement (60-250 ms) ; c'est le comportement avec lequel l'onduleur
    // a été appairé avec succès. TCP_NODELAY=1 envoie tout immédiatement, mais en segments séparés.
    const bool nodelay = env_or("TCP_NODELAY", "0") == "1";
    svr.set_socket_options([nodelay](socket_t sock) {
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        int nd = nodelay ? 1 : 0;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
    });
    svr.set_tcp_nodelay(nodelay);

    svr.set_logger([](const httplib::Request &req, const httplib::Response &res) {
        bool err = res.status >= 400;
        if (!should_log("http " + req.remote_addr + " " + req.method + " " + req.path + " " + req.body, err)) return;
        std::cout << "[HTTP] " << req.remote_addr << " " << req.method << " " << req.path;
        char sep = '?';
        if (req.method == "GET") for (const auto &kv : req.params) { std::cout << sep << kv.first << '=' << kv.second; sep = '&'; }
        std::cout << " -> " << res.status;
        if (req.has_header("Upgrade")) std::cout << " [Upgrade: " << req.get_header_value("Upgrade") << "]";
        if (!req.body.empty()) std::cout << " body=" << trunc(req.body);
        std::cout << "\n       resp=" << trunc(res.body) << std::endl;
    });

    auto send = [](httplib::Response &res, const json &j, int status = 200) {
        res.status = status;
        res.set_content(j.dump(), "application/json");
    };

    // GET /rpc/<Méthode> et POST /rpc/<Méthode> : résultat « nu »
    auto direct = [&](const httplib::Request &req, httplib::Response &res, const json &params) {
        try {
            send(res, rpc_call(req.matches[1], params));
        } catch (const RpcError &e) {
            send(res, {{"code", e.code}, {"message", e.what()}}, 404);
        }
    };

    svr.Get("/shelly",   [&](const httplib::Request &, httplib::Response &res) { send(res, device_info()); });
    svr.Get("/settings", [&](const httplib::Request &, httplib::Response &res) { send(res, gen1_settings()); });

    svr.Get(R"(/rpc/(.+))", [&](const httplib::Request &req, httplib::Response &res) {
        json p = json::object();
        for (const auto &kv : req.params) {
            json v = json::parse(kv.second, nullptr, false);
            p[kv.first] = v.is_discarded() ? json(kv.second) : v;
        }
        direct(req, res, p);
    });

    svr.Post(R"(/rpc/(.+))", [&](const httplib::Request &req, httplib::Response &res) {
        json body = json::parse(req.body, nullptr, false);
        // Le corps peut être une trame JSON-RPC complète ou juste les paramètres
        if (!body.is_discarded() && body.is_object() && body.contains("method")) { send(res, rpc_frame(body)); return; }
        direct(req, res, (!body.is_discarded() && body.is_object()) ? body : json::object());
    });

    svr.Post("/rpc", [&](const httplib::Request &req, httplib::Response &res) {
        json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            send(res, {{"code", -103}, {"message", "Invalid JSON-RPC request"}}, 400);
            return;
        }
        send(res, rpc_frame(body));
    });

    // /rpc en WebSocket : JSON-RPC par trames (utilisé par l'onduleur Hoymiles après l'appairage)
    // Les connexions ouvertes sont recensées pour pouvoir les fermer à l'arrêt (sinon stop() attend).
    static std::mutex ws_mtx;
    static std::set<httplib::ws::WebSocket *> ws_open;
    svr.WebSocket("/rpc", [](const httplib::Request &req, httplib::ws::WebSocket &ws) {
        const std::string who = req.remote_addr;
        { std::lock_guard<std::mutex> l(ws_mtx); ws_open.insert(&ws); }
        const int sock = find_peer_socket(req.remote_addr, req.remote_port);
        if (sock >= 0) {
            int one = 1;   // TCP_NODELAY : la trame part dès le décorkage, sans attendre d'acquittement
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        }
        std::cout << "[WS] " << who << " connecté"
                  << (sock >= 0 ? "" : " (socket introuvable : trames possiblement découpées)") << std::endl;
        const auto t_open = std::chrono::steady_clock::now();
        unsigned long n_msg = 0;
        std::string last_method;
        std::string msg;
        for (;;) {
            auto kind = ws.read(msg);
            if (kind == httplib::ws::Fail) break;
            if (kind != httplib::ws::Text && kind != httplib::ws::Binary) continue;
            json rq = json::parse(msg, nullptr, false);
            if (rq.is_discarded() || !rq.is_object()) {
                std::cerr << "[WS] " << who << " message non JSON : " << trunc(msg) << std::endl;
                continue;
            }
            ++n_msg;
            last_method = rq.value("method", "");
            json resp = rpc_frame(rq);
            std::string out = resp.dump();
            // Répondre dans le même type de trame que la requête (comme uni-meter)
            set_cork(sock, true);
            bool sent = (kind == httplib::ws::Text) ? ws.send(out) : ws.send(out.data(), out.size());
            set_cork(sock, false);   // émet en-tête + longueur + charge dans un seul segment
            if (should_log("ws " + who + " " + rq.value("method", ""), resp.contains("error")))
                std::cout << "[WS] " << who << " <- " << trunc(msg) << "\n     -> " << trunc(out) << std::endl;
            if (!sent) break;
        }
        { std::lock_guard<std::mutex> l(ws_mtx); ws_open.erase(&ws); }
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t_open).count();
        std::cout << "[WS] " << who << " déconnecté après " << secs << " s, " << n_msg
                  << " requête(s), dernière : " << (last_method.empty() ? "-" : last_method) << std::endl;
    });

    // --- Réserver le port AVANT toute annonce mDNS (une 2e instance s'arrête ici sans rien toucher)
    if (!svr.bind_to_port("0.0.0.0", cfg.http_port)) {
        std::cerr << "[HTTP] Impossible d'écouter sur le port " << cfg.http_port
                  << " (déjà utilisé par une autre instance ? droits root ?)" << std::endl;
        MQTTAsync_destroy(&g_client);
        return 1;
    }
    std::cout << "[HTTP] Écoute sur le port " << cfg.http_port << std::endl;

    // --- mDNS
    std::thread publisher;
    if (cfg.mdns_avahi) {
        avahi_write_services();
        publisher = std::thread(avahi_publish_loop);
    }

    // --- Arrêt propre
    std::thread([&] {
        int sig;
        sigwait(&sigs, &sig);
        std::cout << "[INIT] Arrêt (signal " << sig << ")" << std::endl;
        {
            std::lock_guard<std::mutex> l(ws_mtx);
            for (auto *w : ws_open) w->close(httplib::ws::CloseStatus::GoingAway, "shutdown");
        }
        svr.stop();
        // Un client WebSocket qui ne répond pas à la fermeture bloquerait l'arrêt : on force après 3 s
        for (int i = 0; i < 30 && !g_listen_done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!g_listen_done) {
            std::cerr << "[INIT] Arrêt forcé (WebSocket encore ouverte)" << std::endl;
            avahi_unregister();
            std::_Exit(0);
        }
    }).detach();

    bool ok = svr.listen_after_bind();
    g_listen_done = true;

    avahi_unregister();
    if (publisher.joinable()) publisher.join();

    MQTTAsync_disconnectOptions disc = MQTTAsync_disconnectOptions_initializer;
    MQTTAsync_disconnect(g_client, &disc);
    MQTTAsync_destroy(&g_client);
    return ok ? 0 : 1;
}

