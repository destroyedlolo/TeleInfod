/*
 * TeleInfod
 * 	A daemon to publish EDF's "Télé Information" to a MQTT broker
 *
 * 	Compilation
 *
 * if using MOSQUITTO (working as of v0.1 - Synchronous)
gcc -DUSE_MOSQUITTO -lpthread -lmosquitto -Wall TeleInfod.c -o TeleInfod
 *
 * if using PAHO (Asynchronous)
gcc -DUSE_PAHO -lpthread -lpaho-mqtt3c -Wall TeleInfod.c -o TeleInfod
 *
 * Copyright 2015-2021 Laurent Faillie
 *
 * 		TeleInfod is covered by 
 *      Creative Commons Attribution-NonCommercial 3.0 License
 *      (http://creativecommons.org/licenses/by-nc/3.0/) 
 *      Consequently, you're free to use if for personal or non-profit usage,
 *      professional or commercial usage REQUIRES a commercial licence.
 *  
 *      TeleInfod is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Note : I didn't find any information if the TéléInfo frame is French Only or if it's an
 * 	International norm. So, by default, I put comment in English.
 *
 *		03/05/2015 - v0.1 LF - Start of development, using Mosquitto's own library, synchronous
 *		04/05/2015 - v0.2 LF - Add Paho library and asynchronous calls
 *		05/05/2015 - v0.3 LF - main infinite loop implemented
 *							- add retained option when using Paho
 *							- live data moved to .../values/
 *		05/05/2015 - v1.0 LF - Add summary
 *							- release as v1.0
 *		06/05/2015 - v1.1 LF - Send only strings to avoid endianness issue
 *		29/07/2015 - v1.2 LF - Allow 0 sample delay : in this case, ttyS stay open
 *					-------
 *		25/08/2015 - v2.0 LF - Add monitoring period
 *		22/10/2015 - v2.1 LF - Improve Mosquitto handling
 *				- Add fields PTEC, IMAX, ISOUSC, HHPHC, OPTARIF
 *		21/03/2017 - v2.2 LF - in verbose mode, display when new frame arrives
 *		25/07/2017 - v2.3 LF - Add a message when we are waiting for frames
 *		10/09/2017 - v2.4 LF - Add -dd for a better debugging
 *				- Add HHPHC in summary
 *		10/09/2017 - v2.5 LF - Add PTEC in summary
 *					-------
 *		04/06/2021 - v3.0 LF - handly Linky's standard frame
 *							- remove c99 dependancies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <libgen.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

#include <time.h>

#ifdef USE_MOSQUITTO
#	include <mosquitto.h>
#elif defined(USE_PAHO)
#	include <MQTTClient.h>
#endif

#define VERSION "3.0"
#define DEFAULT_CONFIGURATION_FILE "/usr/local/etc/TeleInfod.conf"
#define MAXLINE 1024	/* Maximum length of a line to be read */
#define BRK_KEEPALIVE 60	/* Keep alive signal to the broker */

int debug = 0;

	/*
	 * Helpers
	 */
char *removeLF(char *s){
	size_t l=strlen(s);
	if(l && s[--l] == '\n')
		s[l] = 0;
	return s;
}

char *striKWcmp( char *s, const char *kw ){
/* compare string s against kw
 * Return :
 * 	- remaining string if the keyword matches
 * 	- NULL if the keyword is not found
 */
	size_t klen = strlen(kw);
	if( strncasecmp(s,kw,klen) )
		return NULL;
	else
		return s+klen;
}

char *extr_arg(char *s, int l){ 
/* Extract an argument from TéléInfo trame 
 *	s : argument string just after the token
 *	l : length of the argument
 */
	s++;	/* Skip the leading space */
	s[l]=0;
	return s;
}

	/*
	 * Configuration
	 */
union figures {
	struct {
		int PAPP;			/* power */
		int IINST;			/* Intensity */
		int HCHC;			/* Counter "Heure Creuse" */
		int HCHP;			/* Counter "Heure Plaine" */
		int BASE;			/* Counter "Base" */
		char PTEC[4];		/* Current billing */
		int ISOUSC;			/* Subscribed intensity */
		char HHPHC;			/* Groupe horaire */
		char OPTARIF[4];	/* Billing Option */
		int IMAX;			/* Maximum intensity */
	} historic;
	struct {
		int EAST;			/* Energie active soutirée totale */
		int EAIT;			/* Energie active injectée totale */
		int IRMS1;			/* Courant efficace, phase 1 */
		int URMS1;			/* Tension efficace, phase 1 */
		int PREF;			/* Puissance app. de référence */
		int PCOUP;			/* Puissance app. de coupure */
		int SINSTS;			/* Puissance app. Instantanée soutirée */
		int SMAXSN;			/* Puissance app. max. soutirée n */
		int SMAXSN1;		/* Puissance app max. soutirée n-1 */
		int SINSTI;			/* Puissance app. Instantanée injectée */
		int SMAXIN;			/* Puissance app. max. injectée n */
		int SMAXIN1;		/* Puissance app max. injectée n-1 */
		int UMOY1;			/* Tension moy. ph. 1 */
		int RELAIS;			/* statut relais */
	} standard;
};

struct CSection {	/* Section of the configuration : a TéléInfo flow */
	struct CSection *next;	/* Next section */
	const char *name;		/* help to have understandable error messages */
	pthread_t thread;
	const char *port;		/* Where to read */
	const char *topic;		/* main topic */
	const char *cctopic;	/* Converted Customer topic */
	const char *cptopic;	/* Converted Producer topic */
	char *sumtopic;			/* Summary topic */
	bool standard;			/* true : standard frame / false : historic frame */
	union figures values;	/* actual values */
	union figures max;		/* Maximum values during monitoring period */
};

struct Config {
	struct CSection *sections;
	int delay;
	int period;
	const char *Broker_Host;
	int Broker_Port;
#ifdef USE_MOSQUITTO
	struct mosquitto *mosq;
#elif defined(USE_PAHO)
	MQTTClient client;
#else
#	error "No MQTT library defined"
#endif
} cfg;

void read_configuration( const char *fch){
	FILE *f;
	char l[MAXLINE];
	char *arg;

		/* Default settings */
	cfg.sections = NULL;	/* No sections defined yet */
	cfg.delay = 0;
	cfg.period = 0;

#ifdef USE_PAHO
	cfg.Broker_Host = "tcp://localhost:1883";
	cfg.client = NULL;
#else
	cfg.Broker_Host = "localhost";
	cfg.Broker_Port = 1883;
	cfg.mosq = NULL;
#endif

	if(debug)
		printf("Reading configuration file '%s'\n", fch);

		/* Reading the configuration file */
	if(!(f=fopen(fch, "r"))){
		perror(fch);
		exit(EXIT_FAILURE);
	}

	while(fgets(l, MAXLINE, f)){
		if(*l == '#' || *l == '\n')	/* Ignore comments */
			continue;

		if(*l == '*'){	/* Entering in a new section */
			struct CSection *n = malloc( sizeof(struct CSection) );
			assert(n);
			memset(n, 0, sizeof(struct CSection));	/* Clear all fields to help to generate the summary */
			assert( (n->name = strdup( removeLF(l+1) )) );
			n->port = n->topic = NULL;
			n->cctopic = n->cptopic = NULL;

			n->next = cfg.sections;	/* inserting in the list */
			cfg.sections = n;
	
			if(debug)
				printf("Entering section '%s'\n", n->name);
		}

		if((arg = striKWcmp(l,"Sample_Delay="))){
			cfg.delay = atoi( arg );
			if(debug)
				printf("Delay b/w sample : %ds\n", cfg.delay);

		} else if((arg = striKWcmp(l,"Broker_Host="))){
			assert( (cfg.Broker_Host = strdup( removeLF(arg) )) );
			if(debug)
				printf("Broker host : '%s'\n", cfg.Broker_Host);

		} else if((arg = striKWcmp(l,"Broker_Port="))){
#if defined(USE_PAHO)
			fputs("*F* When using Paho library, Broker_Port directive is not used.\n"
				"*F* Instead, use\n"
				"*F*\tBroker_Host=protocol://host:port\n", stderr);
			exit(EXIT_FAILURE);
#else
			cfg.Broker_Port = atoi( arg );
			if(debug)
				printf("Broker port : %d\n", cfg.Broker_Port);
#endif

		} else if((arg = striKWcmp(l,"Monitoring_Period="))){
			cfg.period = atoi( arg );
			if(debug)
				printf("Monitoring period : %d\n", cfg.period);

		} else if((arg = striKWcmp(l,"Port="))){	/* It's an historic section */
			if(!cfg.sections){
				fputs("*F* Configuration issue : Port directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			if( cfg.sections->port ){
				fputs("*F* Configuration issue : Port directive used more than once in a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (cfg.sections->port = strdup( removeLF(arg) )) );
			cfg.sections->standard = false;

				/* Initialise maxes to invalid values */
			cfg.sections->max.historic.PAPP = cfg.sections->max.historic.IINST = cfg.sections->max.historic.HCHC = cfg.sections->max.historic.HCHP = cfg.sections->max.historic.BASE = -1;

			if(debug)
				printf("\tHistoric frame\n\tSerial port : '%s'\n", cfg.sections->port);

		} else if((arg = striKWcmp(l,"SPort="))){	/* It's a standard section */
			if(!cfg.sections){
				fputs("*F* Configuration issue : SPort directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			if( cfg.sections->port ){
				fputs("*F* Configuration issue : SPort directive used more than once in a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (cfg.sections->port = strdup( removeLF(arg) )) );
			cfg.sections->standard = true;

				/* Initialise maxes to invalid values */
			cfg.sections->max.historic.PAPP = cfg.sections->max.historic.IINST = cfg.sections->max.historic.HCHC = cfg.sections->max.historic.HCHP = cfg.sections->max.historic.BASE = -1;

			if(debug)
				printf("\tStandard frame\n\tSerial port : '%s'\n", cfg.sections->port);

		} else if((arg = striKWcmp(l,"Topic="))){
			if(!cfg.sections){
				fputs("*F* Configuration issue : Topic directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (cfg.sections->topic = strdup( removeLF(arg) )) );
			if(debug)
				printf("\tTopic : '%s'\n", cfg.sections->topic);

		} else if((arg = striKWcmp(l,"ConvCons="))){
			if(!cfg.sections){
				fputs("*F* Configuration issue : ConvCons directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (cfg.sections->cctopic = strdup( removeLF(arg) )) );
			if(debug)
				printf("\tConverted customer topic : '%s'\n", cfg.sections->cctopic);

		} else if((arg = striKWcmp(l,"ConvProd="))){
			if(!cfg.sections){
				fputs("*F* Configuration issue : ConvProd directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (cfg.sections->cptopic = strdup( removeLF(arg) )) );
			if(debug)
				printf("\tConverted producer topic : '%s'\n", cfg.sections->cptopic);
		}

	}

	if(debug)
		puts("");

	fclose(f);
}

#ifdef USE_MOSQUITTO
int papub( const char *topic, int length, void *payload, int retained ){	/* Custom wrapper to publish */
	switch(mosquitto_publish(cfg.mosq, NULL, topic, length, payload, 0, retained ? true : false)){
	case MOSQ_ERR_INVAL:
		fputs("The input parameters were invalid",stderr);
		break;
	case MOSQ_ERR_NOMEM:
		fputs("An out of memory condition occurred",stderr);
		break;
	case MOSQ_ERR_NO_CONN:
		fputs("The client isn’t connected to a broker",stderr);
		break;
	case MOSQ_ERR_PROTOCOL:
		fputs("There is a protocol error communicating with the broker",stderr);
		break;
	case MOSQ_ERR_PAYLOAD_SIZE:
		fputs("Payloadlen is too large",stderr);
		break;
	}

	return 1;
}
#elif USE_PAHO
	/*
	 * Paho's specific functions
	 */
int msgarrived(void *ctx, char *topic, int tlen, MQTTClient_message *msg){
	if(debug)
		printf("*I* Unexpected message arrival (topic : '%s')\n", topic);

	MQTTClient_freeMessage(&msg);
	MQTTClient_free(topic);
	return 1;
}

void connlost(void *ctx, char *cause){
	printf("*W* Broker connection lost due to %s\n", cause);
}

int papub( const char *topic, int length, void *payload, int retained ){	/* Custom wrapper to publish */
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.retained = retained;
	pubmsg.payloadlen = length;
	pubmsg.payload = payload;

	return MQTTClient_publishMessage( cfg.client, topic, &pubmsg, NULL);
}
#endif

	/*
	 * Processing
	 */

void handleInt(int na){
	exit(EXIT_SUCCESS);
}

void theend(void){
		/* Some cleanup */
#ifdef USE_MOSQUITTO
	mosquitto_destroy(cfg.mosq);
	mosquitto_lib_cleanup();
#elif defined(USE_PAHO)
	MQTTClient_disconnect(cfg.client, 10000);	/* 10s for the grace period */
	MQTTClient_destroy(&cfg.client);
#endif
}

int main(int ac, char **av){
	const char *conf_file = DEFAULT_CONFIGURATION_FILE;
	pthread_attr_t thread_attr;

	if(ac > 0){
		int i;
		for(i=1; i<ac; i++){
			if(!strcmp(av[i], "-h")){
				fprintf(stderr, "%s (%s)\n"
					"Publish TéléInfo figure to an MQTT broker\n"
					"Known options are :\n"
					"\t-h : this online help\n"
					"\t-d : enable debug messages\n"
					"\t-dd : enable debug messages and display frame\n"
					"\t-f<file> : read <file> for configuration\n"
					"\t\t(default is '%s')\n",
					basename(av[0]), VERSION, DEFAULT_CONFIGURATION_FILE
				);
				exit(EXIT_FAILURE);
			} else if(!strcmp(av[i], "-d") || !strcmp(av[i], "-dd")){
				debug = !strcmp(av[i], "-dd") ? 2:1;
				puts("TeleInfod (c) L.Faillie 2015-21");
				printf("%s (%s) starting ...\n", basename(av[0]), VERSION);
			} else if(!strncmp(av[i], "-f", 2))
				conf_file = av[i] + 2;
			else {
				fprintf(stderr, "Unknown option '%s'\n%s -h\n\tfor some help\n", av[i], av[0]);
				exit(EXIT_FAILURE);
			}
		}
	}
	read_configuration( conf_file );

		/* Sanity checks */
	struct CSection *s = cfg.sections;

	if(!s){
		fputs("*F* No section defined : giving up ...\n", stderr);
		exit(EXIT_FAILURE);
	}

	for( ; s; s = s->next ){
		if( !s->port ){
			fprintf( stderr, "*F* No port defined for section '%s'\n", s->name );
			exit(EXIT_FAILURE);
		}

		if( s->standard ){	/* check specifics for standard frames */
			if( !s->topic && !s->cctopic && !s->cptopic ){
				fprintf( stderr, "*F* at least Topic, ConvCons or ConvProd has to be provided for standard section '%s'\n", s->name );
				exit(EXIT_FAILURE);
			}
			if( s->cctopic ){
				fprintf( stderr, "*F* ConvCons is not yet implemented as per v3.0 in standard section '%s'\n", s->name );
				exit(EXIT_FAILURE);
			}
		} else {	/* check specifics for historic frames */
			if( !s->topic ){
				fprintf( stderr, "*F* Topic is mandatory for historic section '%s'\n", s->name );
				exit(EXIT_FAILURE);
			}
		}
	}

	exit(EXIT_SUCCESS);
}

