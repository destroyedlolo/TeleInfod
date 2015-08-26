**TeleInfod** is a daemon that publishes electricity smart meters “*TéléInformation*” data to a **MQTT Broker**.

#Requirements :#
* A smart meter providing “TéléInformation” data as a serial flow (information about a DIY level converter for BananaPI can be found on [my web site](http://destroyedlolo.info/BananaPI/TeleInformation/) - French only -)
* MQTT broker (I personally use [Mosquitto](http://mosquitto.org/) )
* Even if TeleInfod can be compiled to use Mosquitto’s own library, it is strongly advised to use [Paho](http://eclipse.org/paho/) as MQTT communication layer.

#Installation :#
* Get TeleInfod.c and put it in a temporary directory
* If you want to use MOSQUITTO’s own library :
	* Install MOSQUITTO :)
	* Compile TeleInfod using the following command line :
```
    gcc -std=c99 -DUSE_MOSQUITTO -lpthread -lmosquitto -Wall TeleInfod.c -o TeleInfod
```
* If you want to use  PAHO library
	* Install PAHO
	* Compile TeleInfod using the following command line :
```
    gcc -std=c99 -DUSE_PAHO -lpthread -lpaho-mqtt3c -Wall TeleInfod.c -o TeleInfod
```

#Launch options :#
TeleInfod knows the following options :
* *-d* : verbose output
* *-f<file>* : loads <file> as configuration file

#Configuration file :#
Without *–f* option, the configuration file is by default : “*/usr/local/etc/TeleInfod.conf*”
Following general directives are known :
* *Broker_Host=* where the Broker can be reached.
Using moquitto library, only the hostname has to be provided (as “localhost”, “myhost.mydomain.tld” ).
Using Paho, use an URL like tcp://<hostname>:port (as tcp://localhost:1883).
* *Broker_Port=* the port to connect to (only when using Mosquitto library)
* *Sample_Delay=* Delay b/w 2 samples (in seconds, default delay = 30s). Due to 3.4.xx kernel instability on multiple open()/close() of ttyS, it is advised to set this parameter to **0** which keep ttyS open and send data as much as provided.
* *Monitoring_Period=* (in seconds, introduced in v2.0). Daily between 2 samples of a subscribing monitoring tool to '.../summary' topics (see bellow).
If unset or null, actual values are sent as previously.
If set, maximum values read during the period is sent, and additional fields are added for counters

One or more section has be defined as :

```
    *Production
    Port=/dev/ttyS2
    Topic=/TeleInfo/Production
```

Where
* The line starting with a star indicates a new section. The name after the star is for information only (useful only to know which section is faulty in case of error)
* *Port=* path to the serial port to use (which has to be configured before launching TeleInfod
* *Topic=* Root of the topic tree where to expose data

In the case above, the following tree will be created :
* */TeleInfo/Production/values/IINST* – « Intensité instantanée »
* */TeleInfo/Production/values/PINST* – « Puissance instantanée »
* */TeleInfo/Production/values/BASE* – BASE counter
* */TeleInfo/Production/values/BASEd* – BASE counter difference vs the previous value sent
* …
* */TeleInfo/Production/summary* – concatenation of all values above in *JSON* format. This value is “*retained*”, meaning the broker will reply immediately with last values sent. This topic is mostly used to feed monitoring tools (like my very own **Domestik**) to graph some trends, without having to wait for fresh data. Whereas “Values” topics aim to push/refresh actual data on “live” dashboard.
Each section runs in its own thread, so will not block others if the data line doesn’t send anything.

Notez-bien : TeleInfod has been made to suit my needs. Consequently it handles up to now only *BASE* (i.e. for photovoltaic production), and “*Heure Creuse*” contracts. Contributions are obviously welcomed if you want to add others.
