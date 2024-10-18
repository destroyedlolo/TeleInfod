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
* **Broker_Host=** - le serveur hébergeant le broker MQTT. Avec la librairie Mosquitto, seul son nom doit être fourni (par exemple `localhost` ou encore `myhost.mydomain.tld`).<br>
Avec la bibliothèque Paho, il faut fournir une URL `tcp://<hostname>:port` (comme `tcp://localhost:1883`).
* **Broker_Port=** - le port de connexion du broker MQTT (seulement pour la bibliothèque Mosquitto)

Au moins une section doit être définie :
```
    *Production
    Port=/dev/ttyS2
    Topic=/TeleInfo/Production
```

Avec
* La ligne commençant par une étoile ** * ** indique le début de la section. Suit son *nom* qui vous sera utile pour identifier les messages si vous avez plusieurs compteurs et donc plusieurs sections.
* *Port=* path to the serial port to use (which has to be configured before launching TeleInfod
* *Topic=* Root of the topic tree where to expose data

In the case above, the following tree will be created :
* */TeleInfo/Production/values/IINST* – « Intensité instantanée »
* */TeleInfo/Production/values/PINST* – « Puissance instantanée »
* */TeleInfo/Production/values/BASE* – BASE counter
* */TeleInfo/Production/values/BASEd* – BASE counter difference vs the previous value sent
* …
* */TeleInfo/Production/summary* – concatenation of all values above in *JSON* format. This value is “*retained*”, meaning the broker will reply immediately with last values sent. This topic is mostly used to feed monitoring tools (like my very own **Domestik**) to graph some trends, without having to wait for fresh data. Whereas “Values” topics aim to push/refresh actual data on the “*live*” dashboard.<br>
Each section runs in its own thread, so it will not block others if the data line doesn’t send anything.

## Section for Standard mode

**SPort=** enable reading Standard frames, which been introduced by Linky.

```
    *Consommation
    SPort=/dev/ttyS3
    Topic=TeleInfo/Consommation
    ConvCons=TeleInfo/HistConso
    ConvProd=TeleInfo/HistProd
```

Where
* **SPort=** path to the serial port to use (which has to be configured before launching TeleInfod)
* **Topic=** Root of the topic tree where to expose data with "standards" labels
(for *standard* frames, the main topic is optional, as long as ConvCons and/or ConvProd is present)

And in addition 

* **ConvProd=** convert standard data to historic's name for producer in order to stay compatible with ancient software
* **ConvCons=** convert standard data to historic's name for consumers in order to stay compatible with ancient software ( *not yet supported as per 3.0* )

### ConvCons conversion

In historic mode, the mode for a producer counter is "BASE".
Consequently, only the following values are meaningful :

Field name | Standard | converted topic
-----------|----------|-----
*Puissance app. Instantanée injectée* | **SINSTI** | .../values/**PAPP**
*Courant efficace* | **IRMS1** | .../values/**IINST**
*Energie active injectée totale* | **EAIT** | .../values/**BASE**
*Puissance app. max. injectée n* | **SMAXIN** | .../values/**IMAX** (/200)

---

> [!NOTE]  
> Notez-bien : TeleInfod has been made to suit my needs. Consequently 
> - with *historic* frames, it handles up to now only *BASE* (i.e. for photovoltaic production), and “*Heure Creuse*” contracts. Contributions are obviously welcome if you want to add others.
> - It can only convert *standard* frame to producer-compatible historic data (my own consumer linky is configured for historic frame as there is no added value that is worth buying a new adapter : *don't hesitate to contribute*)
> - with *standard* frames, `.../Values/Date` is published as per DATE field. It is converted to ISO 8601 format, and the timezone is applied to reflect Linky *DST*. As of 3.1, this timezone is hard-coded only for **France Metropolitain** (GMT+1).
	
HELP ARE WELCOME TO COVER OTHER SITUATIONS.

====

# Reference

- Linky and historic trames reference : **Enedis-NOI-CPT_54E**
