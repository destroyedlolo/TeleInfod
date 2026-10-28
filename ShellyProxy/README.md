# Passerelle TeleInfo vers Shelly Pro 3EM (C++)

Une passerelle C++ légère et haute performance permettant de simuler un compteur d'énergie **Shelly Pro 3EM** (Gen 2) à partir des données de téléinformation Linky publiées sur MQTT (par exemple via [`teleinfod`](https://github.com/destroyedlolo/TeleInfod)).

Ce projet est spécialement conçu pour intégrer les données de consommation et d'injection Linky dans des systèmes fermés de gestion de batterie ou d'onduleurs réseau tels que **Hoymiles (S-Miles, batterie MS-A2)** ou **Marstek**.\
\
Ce projet a été entièrement vibe-codé avec beaucoup d’efforts pour qu’il fonctionne enfin. Il n’y a donc strictement aucune garanti et, à date, je n’ai pas l’intention de passer plus de temps pour rendre son code plus civilisé : juste un pensement “rapide” pour compenser le manque d’ouverture de mon onduleur.

## 📋 Table des matières

* [Aperçu & Objectif](#-aperçu--objectif)

* [Fonctionnalités Clés](#-fonctionnalités-clés)

* [Correspondance des Étiquettes Linky](#-correspondance-des-étiquettes-linky)

* [Convention de Signe](#-convention-de-signe)

* [Prérequis système](#-prérequis-système)

* [Compilation](#-compilation)

* [Configuration (Variables d'Environnement)](#-configuration-variables-denvironnement)

* [Mécanismes Avancés (mDNS & WebSocket TCP_CORK)](#-mécanismes-avancés-mdns--websocket-tcp_cork)

* [Tests et Validation](#-tests-et-validation)

* [Licence](#-licence)

## 🎯 Aperçu & Objectif

L'application **Hoymiles S-Miles** et les batteries résidentielles (comme la **Hoymiles MS-A2**) s'attendent à dialoguer localement avec un compteur d'énergie triphasé/monophasé **Shelly Pro 3EM** via des requêtes HTTP JSON-RPC 2.0 ou des liaisons WebSocket persistent.

Plutôt que d'installer une véritable pince ampermétrique Shelly physique dans votre tableau électrique, ce démon intercepte les trames téléinfo issues du compteur Linky transmises par MQTT et expose exactement les mêmes endpoints HTTP/WebSocket, formats JSON et requêtes mDNS qu'un vrai Shelly Pro 3EM.

```
┌─────────────────┐       MQTT       ┌──────────────────┐    HTTP/WS (port 80)    ┌───────────────────┐
│ Compteur Linky  │ ───────────────> │  shelly_pro3em   │ <─────────────────────> │ Écosystème        │
│   (teleinfod)   │  sinsts, east..  │   (Démon C++)    │     JSON-RPC / mDNS     │ Hoymiles S-Miles  │
└─────────────────┘                  └──────────────────┘                         └───────────────────┘

```

## ✨ Fonctionnalités Clés

* **API JSON-RPC 2.0 Shelly Gen 2 intégrale** : Support des routes `/rpc/EM.GetStatus`, `/rpc/EMData.GetStatus`, `/rpc/Shelly.GetStatus`, `/rpc/Sys.GetStatus`, etc.

* **Support WebSocket (`/rpc`)** : Maintien de connexion persistante avec réémission en trames uniques grâce à `TCP_CORK`.

* **Découverte automatique mDNS (Avahi)** : Publication automatique des services `_shelly._tcp` et `_http._tcp` pour un appairage transparent dans S-Miles.

* **Support UDP JSON-RPC** : Compatible avec d'autres équipements (ex: batteries Marstek sur le port UDP 1010).

* **Consommation mémoire et CPU minimales** : Écrit en C++17 natif sans framework lourd.

Voici le contenu complet et détaillé à partir de la section **Correspondance des Étiquettes Linky** :

---

## Correspondance des Étiquettes Linky (Mode Standard Monophasé)

Le démon traduit les étiquettes issues du compteur Linky (publiées par `teleinfod` ou un pont équivalent sous l'arborescence `<TOPIC_PREFIX>/<étiquette>`) en mesures pour un Shelly Pro 3EM.

Afin de simuler un raccordement monophasé sur un appareil nativement triphasé, **toutes les mesures sont affectées à la phase A** (les phases B et C sont maintenues à 0 W, 0 A, 230 V) :

| Étiquette Téléinfo / Source | Attribut Shelly Pro 3EM équivalent | Unité & Description |
| --- | --- | --- |
| **`SINSTS` - `SINSTI**` *(si `POWER_MODE=net`)* | `a_act_power`, `total_act_power` | **Watts (W)** — Puissance active nette (négative si injection sur le réseau (Surplus solaire))|
| **`SINSTS`** *(si `POWER_MODE=import`)* | `a_act_power`, `total_act_power` | **Watts (W)** — Puissance active soutirée uniquement.|
| **`|SINSTS - SINSTI|`** | `a_aprt_power`, `total_aprt_power` | **VA** — Puissance apparente (assimilée à la valeur absolue de la puissance active).|
| **`URMS1`** *(à défaut `UMOY1`, ou `VOLTAGE`)* | `a_voltage` | **Volts (V)** — Tension efficace réseau. |
| **`IRMS1`** *(à défaut $\frac{\Vert{}P\Vert{}}{U}$)* | `a_current`, `total_current` | **Ampères (A)** — Intensité efficace sur la phase A.|
| **`EAST`** | `EMData:0` $\rightarrow$ `a_total_act_energy`, `total_act` | **Wh** — Index d'énergie totale soutirée (Import).|
| **`EAIT`** | `EMData:0` $\rightarrow$ `a_total_act_ret_energy`, `total_act_ret` | **Wh** — Index d'énergie totale injectée (Export / Surplus). |
| *`NTARF`, `LTARF`, `EASF01`, `EASF02`, etc.* | *Ignorés* | Métadonnées tarifaires sans équivalent direct dans l'API Shelly. |

---

## Dépendances requises

Pour compiler et exécuter le projet, votre système doit disposer des bibliothèques suivantes :

* **Eclipse Paho MQTT C** (`libpaho-mqtt-as` / `libpaho-mqtt-dev`) — Version asynchrone sécurisée.
* **`cpp-httplib`** (`>= 0.33` requise pour la prise en charge native des WebSockets).
* **`nlohmann-json`** — Manipulation et sérialisation JSON.
* **Avahi** (`avahi-daemon`, `avahi-utils`) — Publication mDNS et résolution de nom locale.

### Installation des paquets

* **Sous Arch Linux :**
```bash
sudo pacman -S paho-mqtt-c cpp-httplib nlohmann-json avahi
```
* **Sous Debian / Ubuntu / Raspberry Pi OS :**
```bash
sudo apt-get update
sudo apt-get install libpaho-mqtt-dev nlohmann-json3-dev avahi-daemon avahi-utils build-essential
```
(Si la version de `cpp-httplib` proposée par votre distribution est inférieure à 0.33, téléchargez le header compatible directement dans votre dossier de projet) :
```bash
curl -LO https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.63.0/httplib.h
```

---

## Compilation

Le binaire se compile avec `g++` en standard C++17 :

```bash
g++ -std=c++17 -O2 -I. -o shelly_pro3em shelly_pro3em.cpp -lpaho-mqtt3as -pthread

```

---

## Variables d'Environnement (Configuration)

Le binaire se configure intégralement au lancement via les variables d'environnement suivantes :

| Variable | Valeur par défaut | Description |
| --- | --- | --- |
| `MQTT_URI` | `tcp://localhost:1883` | Adresse du broker MQTT (ex: `tcp://192.168.1.50:1883`). |
| `MQTT_USER` | *(vide)* | Nom d'utilisateur MQTT (si authentification active). |
| `MQTT_PASSWORD` | *(vide)* | Mot de passe MQTT (si authentification active). |
| `TOPIC_PREFIX` | `TeleInfo/LinkyConsommation` | Arborescence racine MQTT des étiquettes téléinfo. |
| `POWER_MODE` | `net` | `net` = calcul signé (`SINSTS - SINSTI`) ; `import` = `SINSTS` seul. |
| `IFACE` | *(auto)* | Interface réseau à utiliser (`eth0`, `wlan0`...). Par défaut : 1ère IPv4 non-loopback. |
| `SHELLY_MAC` | *(auto MAC interface)* | Adresse MAC (12 caractères hexadécimaux) pour forcer l'identifiant de l'appareil. |
| `SHELLY_HOSTNAME` | `ShellyPro3EM-<MAC>` | Nom de l'hôte mDNS annoncé sur le réseau (`.local`). |
| `HTTP_PORT` | `80` | Port d'écoute HTTP / WebSocket (imposé sur le port 80 par Hoymiles). |
| `UDP_RPC_PORT` | `0` | Port UDP RPC (`0` = désactivé, passer à `1010` pour batterie Marstek). |
| `VOLTAGE` | `230` | Tension par défaut si ni `URMS1` ni `UMOY1` ne sont reçus par MQTT. |
| `MDNS` | `avahi` | `avahi` (crée les fichiers dans `/etc/avahi/services`) ou `none`. |
| `VERBOSE` | `0` | Passer à `1` pour afficher toutes les requêtes/réponses dans la console. |
| `TCP_NODELAY` | `0` | Passer à `1` pour désactiver l'algorithme de Nagle TCP. |

---

## Exécution & Exemple de Service Systemd

Puisque le programme écoute sur le port privilégié 80, il nécessite les privilèges `root` ou l'attribution préalable des capacités réseau (`sudo setcap 'cap_net_bind_service=+ep' ./shelly_pro3em`).

### Lancement manuel

```bash
sudo TOPIC_PREFIX="TeleInfo/Production/values" ./shelly_pro3em
```

### Installation en service d'arrière-plan (`systemd`)

Créez le fichier `/etc/systemd/system/shelly-pro3em.service` :

```ini
[Unit]
Description=Passerelle Téléinfo vers Shelly Pro 3EM Virtuel
After=network.target mosquitto.service avahi-daemon.service

[Service]
Type=simple
ExecStart=/usr/local/bin/shelly_pro3em
Environment=MQTT_URI=tcp://localhost:1883
Environment=TOPIC_PREFIX=TeleInfo/Production/values
Environment=POWER_MODE=net
Environment=VERBOSE=0
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Activez et démarrez ensuite le service :

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now shelly-pro3em.service

```
