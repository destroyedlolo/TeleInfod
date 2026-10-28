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

## 📊 Correspondance des Étiquettes Linky

Le démon traduit les valeurs téléinfo (mode standard monophasé) vers l'API Shelly Pro 3EM (toute la charge est affectée à la **Phase A**, les phases B et C restent à zéro) :

| Étiquette Linky | Description Téléinfo | Champ Shelly Pro 3EM | Unité | 
| ----- | ----- | ----- | ----- | 
| `SINSTS` - `SINSTI` | Puissance Soutirée - Injectée | `EM.GetStatus` -> `a_act_power` / `total_act_power` | W | 
| `|SINSTS - SINSTI|` | Puissance Apparente instantanée | `EM.GetStatus` -> `a_aprt_power` / `total_aprt_power` | VA | 
| `URMS1` (ou `UMOY1`) | Tension efficace instantanée | `EM.GetStatus` -> `a_voltage` | V | 
| `IRMS1` | Courant efficace (ou calculé \$I = \\ |  |  | 
