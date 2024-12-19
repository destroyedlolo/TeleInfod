> [!IMPORTANT]
> Téléinfo TIC seems only applicable in France. Consequently, this README is only in French.

**TeleInfod** est un démon qui publie les données de votre compteur Enedis par l'intermédiaire de sa prise *Téléinformation* vers un broker **MQTT**. Il est compatible à la fois avec les compteurs **Linky** mais aussi avec les anciens compteurs numériques.

# Dépendances

* Bien évidemment, un (ou des) compteur disposant d'une prise *TéléInformation* "TIC". Mon [site web](http://destroyedlolo.info/BananaPI/TeleInformation/) contient des montages d'exemples pour convertir ce signal (attention, certains montagnes ne sont pas compatibles avec le Linky).
* Un broker MQTT tel que [Mosquitto](http://mosquitto.org/)
* Bien que TeleInfod puisse être compilé avec la librairie Mosquitto, je vous conseille d'utiliser [Paho](http://eclipse.org/paho/).

# Installation :

* Récupérez le code source `TeleInfod.c` et placez-le dans un répertoire temporaire ( `/tmp` fera d'ailleurs parfaitement l'affaire),
* Si vous souhaitez utiliser la librairie **Mosquitto** :
	* Installez Mosquitto :)
	* Compilez TeleInfod comme suit :
```
    gcc -std=c99 -DUSE_MOSQUITTO -lpthread -lmosquitto -Wall TeleInfod.c -o TeleInfod
```
* Si vous souhaitez utiliser la librairie PAHO :
	* Installez PAHO
	* Compilez TeleInfod comme suit :
```
    gcc -std=c99 -DUSE_PAHO -lpthread -lpaho-mqtt3c -Wall TeleInfod.c -o TeleInfod
```

# Launch options :

**TeleInfod** se lance en ligne de commande et reconnait les options suivantes  :
* `-d` ou `-v` : est verbeux, affiche des messages d'information,
* `-f<file>` : utilise <file> comme fichier de configuration. Par défaut, il recherche `/usr/local/etc/TeleInfod.conf`

# Contenu du fichier de configuration :

Les directives générales sont reconnues :
* **Broker_Host=** le serveur hébergeant le broker MQTT. Avec la librairie Mosquitto, seul son nom doit être fourni (par exemple `localhost` ou encore `myhost.mydomain.tld`).<br>
Avec la bibliothèque Paho, il faut fournir une URL `tcp://<hostname>:port` (comme `tcp://localhost:1883`).
* **Broker_Port=** le port de connexion du broker MQTT (seulement pour la bibliothèque Mosquitto)

Au moins une section doit être définie.

> [!TIP]
> Chaque section s'exécute dans un "thread" séparé ; si un flux s'interrompt, il ne bloquera donc pas les autres.

## Exemple pour une trame **historique**.

Ces trames sont compatibles avec les anciens compteurs numériques et les Linky si configurés pour. Le signal est plus lent, 1200 bauds, et compatible avec plus d'optocoupleurs. Mais moins d'informations sont fournies, en particulier, il manque la puissance injectée dans le réseau pour les producteurs qui ne revendent pas 100% de leur production.

```
    *Consommation
    Port=/dev/ttyS2
    Topic=/TeleInfo/Consommation
    Publish=OPTARIF,ISOUSC,BASE,HCHC,HCHP,PTEC,IINST,ADPS,IMAX,PAPP,HHPHC
```

Avec
* La ligne commençant par une étoile `*` indique le début de la section. Suit son *nom* qui vous sera utile pour identifier les messages si vous avez plusieurs compteurs et donc plusieurs sections.
* **Port=** Le port série connecté au compteur (il doit avoir été configuré AVANT de lancer TeleInfod, **1200 bauds, 7 bits, parité paire, 1 bit de stop**). 
* **Topic=** Racine des topics à publier.
* **Publish=** Liste des champs à publier, tels que définis dans la note *Enedis-NOI-CPT_54E*.

Ce qui publiera :
* */TeleInfo/Consommation/values/OPTARIF* – « Option tarifaire »
* */TeleInfo/Consommation/values/ISOUSC* – « Intensité souscrite »
* */TeleInfo/Consommation/values/BASE* – BASE counter
* …

## Exemple pour une trame **Standard**

A ce jour, seul Linky la génère (mais doit avoir été configuré pour).

```
    *Production
    SPort=/dev/ttyS4
    Topic=TeleInfo/Production
    ConvCons=TeleInfo/HistConso
    ConvProd=TeleInfo/HistProd
    ConvProd=TeleInfo/Test/HistProduct
    ConvCons=TeleInfo/Test/HistConso
    Publish=DATE,NGTF,LTARF,EAST,EAIT,IRMS1,URMS1,PREF,PCOUP,SINSTS,SMAXSN,SMAXSN-1,SINSTI,SMAXIN,SMAXIN-1,CCASN,CCASN-1,CCAIN,CCAIN-1,UMOY1,MSG1,MSG2,RELAIS,NTARF,PPOINTE
```

Avec :
* La ligne commençant par une étoile `*` indique le début de la section. Suit son *nom* qui vous sera utile pour identifier les messages si vous avez plusieurs compteurs et donc plusieurs sections.
* **SPort=** Le port série connecté au compteur (il doit avoir été configuré AVANT de lancer TeleInfod, **9600 bauds, 7 bits, parité paire, 1 bit de stop**).
* **Topic=** Racine des topics à publier.
* **Publish=** Liste des champs à publier, tels que définis dans la note *Enedis-NOI-CPT_54E*.

Auquel se rajoutent

* **ConvProd=** Racine des topics convertis correspondant à un producteur
* **ConvCons=** Racine des topics convertis correspondant à un consommateur

## Conversions

Le mécanisme de conversion extrait d'une trame *standard* les informations qui permettront de générer les topics pour producteur et consommateur correspondant à des trames *historique*. Le but est d'apporter une compatibilité avec d'anciens logiciels.<br>
Par exemple, en mode *historique*, un compteur producteur publie son compteur dans le champs `BASE`.

Ce qui donne pour les tableaux suivants :

### Producteur

Champ | Standard | Topic converti
-----------|----------|-----
*Puissance app. Instantanée injectée* | **SINSTI** | .../values/**PAPP**
*Courant efficace* | **IRMS1** | .../values/**IINST**
*Energie active injectée totale* | **EAIT** | .../values/**BASE**
*Puissance app. max. injectée n* | **SMAXIN** | .../values/**IMAX**

### Consommateur 

Champ | Standard | Topic converti
-----------|----------|-----
*Puissance app. Instantanée injectée* | **SINSTS** | .../values/**PAPP**
*Courant efficace* | **IRMS1** | .../values/**IINST**

ps : Les compteurs seront convertis ... lorsque mon compteur Linky de consommation aura basculé en mode standard.

# Document de référence

- **Enedis-NOI-CPT_54E**
