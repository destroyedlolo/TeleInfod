/*
 * TeleInfod
 * 	A daemon to publish EDF's "Télé Information" to a MQTT broker
 *
 * 	Compilation
 *
 * if using MOSQUITTO (working as of v0.1 - Synchronous)
gcc -std=c99 -DUSE_MOSQUITTO -lpthread -lmosquitto -Wall TeleInfod.c -o TeleInfod
 *
 * if using PAHO (Asynchronous)
gcc -std=c99 -DUSE_PAHO -lpthread -lpaho-mqtt3c -Wall TeleInfod.c -o TeleInfod
 *
 * Copyright 2015 Laurent Faillie
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

#include <time.h>
extern char *ctime_r (const time_t *timep, char *buf); /* Not in C99 standard but needed in multi-threaded env */

#ifdef USE_MOSQUITTO
#	include <mosquitto.h>
#elif defined(USE_PAHO)
#	include <MQTTClient.h>
#endif

#define VERSION "2.5"
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

char *mystrdup(const char *as){
	/* as strdup() is missing within C99, grrr ! */
	char *s;
	assert(as);
	assert(s = malloc(strlen(as)+1));
	strcpy(s, as);
	return s;
}
#define strdup(s) mystrdup(s)

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
struct figures {
	int PAPP;				/* power */
	int IINST;				/* Intensity */
	int HCHC;				/* Counter "Heure Creuse" */
	int HCHP;				/* Counter "Heure Plaine" */
	int BASE;				/* Counter "Base" */
	char PTEC[4];			/* Current billing */
	int ISOUSC;				/* Subscribed intensity */
	char HHPHC;				/* Groupe horaire */
	char OPTARIF[4];		/* Billing Option */
	int IMAX;				/* Maximum intensity */
};

struct CSection {	/* Section of the configuration : a TéléInfo flow */
	struct CSection *next;	/* Next section */
	pthread_t thread;
	const char *port;		/* Where to read */
	const char *topic;		/* Broker's topic */
	char *sumtopic;			/* Summary topic */
	struct figures values;	/* actual values */
	struct figures max;		/* Maximum values during monitoring period */
};

struct Config {
	struct CSection *sections;
	const char *Broker_Host;
	int Broker_Port;
	int delay;
	int period;
#ifdef USE_MOSQUITTO
	struct mosquitto *mosq;
#elif defined(USE_PAHO)
	MQTTClient client;
#endif
} cfg;

void read_configuration( const char *fch){
	FILE *f;
	char l[MAXLINE];
	char *arg;

	cfg.sections = NULL;
#ifdef USE_PAHO
	cfg.Broker_Host = "tcp://localhost:1883";
#else
	cfg.Broker_Host = "localhost";
	cfg.Broker_Port = 1883;
#endif
	cfg.delay = 30;
	cfg.period = 0;
#ifdef USE_MOSQUITTO
	cfg.mosq = NULL;
#else
	cfg.client = NULL;
#endif

	if(debug)
		printf("Reading configuration file '%s'\n", fch);

	if(!(f=fopen(fch, "r"))){
		perror(fch);
		exit(EXIT_FAILURE);
	}

	while(fgets(l, MAXLINE, f)){
		if(*l == '#' || *l == '\n')
			continue;

		if(*l == '*'){	/* Entering in a new section */
			struct CSection *n = malloc( sizeof(struct CSection) );
			assert(n);
			memset(n, 0, sizeof(struct CSection));	/* Clear all fields to help to generate the summary */
			n->max.PAPP = n->max.IINST = n->max.HCHC = n->max.HCHP = n->max.BASE = -1;
			n->port = n->topic = NULL;

			n->next = cfg.sections;

			cfg.sections = n;
			if(debug)
				printf("Entering section '%s'\n", removeLF(l+1));
		}

		if((arg = striKWcmp(l,"Sample_Delay="))){
			cfg.delay = atoi( arg );
			if(debug)
				printf("Delay b/w sample : %ds\n", cfg.delay);
		} else if((arg = striKWcmp(l,"Broker_Host="))){
			assert( cfg.Broker_Host = strdup( removeLF(arg) ) );
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
		} else if((arg = striKWcmp(l,"Port="))){
			if(!cfg.sections){
				fputs("*F* Configuration issue : Port directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( cfg.sections->port = strdup( removeLF(arg) ));
			if(debug)
				printf("\tSerial port : '%s'\n", cfg.sections->port);
		} else if((arg = striKWcmp(l,"Topic="))){
			if(!cfg.sections){
				fputs("*F* Configuration issue : Topic directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( cfg.sections->topic = strdup( removeLF(arg) ));
			if(debug)
				printf("\tTopic : '%s'\n", cfg.sections->topic);
		}

	}

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
void *process_flow(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *ftrame;
	char l[MAXLINE];
	char *arg;
	char val[12];

	if(!ctx->topic){
		fputs("*E* configuration error : no topic specified, ignoring this section\n", stderr);
		pthread_exit(0);
	}
	assert( ctx->sumtopic = malloc( strlen(ctx->topic)+9 ) );	/* + "/summary" + 1 */
	strcpy( ctx->sumtopic, ctx->topic );
	strcat( ctx->sumtopic, "/summary" );

	if(!ctx->port){
		fprintf(stderr, "*E* configuration error : no port specified for '%s', ignoring this section\n", ctx->topic);
		pthread_exit(0);
	}
	if(debug)
		printf("Launching a processing flow for '%s'\n", ctx->topic);

	for(;;){
		if(!(ftrame = fopen( ctx->port, "r" ))){
			perror(ctx->port);
			exit(EXIT_FAILURE);
		}

		if(debug)
			printf("*d* Waiting for beginning of a frame for '%s'\n", ctx->port);

		while(fgets(l, MAXLINE, ftrame))	/* Wait 'till the beginning of the trame */
			if(!strncmp(l,"ADCO",4))
				break;
		if(feof(ftrame)){
			fclose(ftrame);
			break;
		}

		if(debug)
			printf("*d* Let's go for '%s'\n", ctx->port);

		while(fgets(l, MAXLINE, ftrame)){	/* Read payloads */
			if(debug > 1)
				printf(l);
			if(!strncmp(l,"ADCO",4)){ /* Reaching the next one */
					/* publish summary */
				if(!cfg.period){	/* No period specified, sending actual values */
					sprintf(l, "{\n\"PAPP\": %d,\n\"IINST\": %d,\n\"HHPHC\": \"%c\",\n\"PTEC\": \"%.4s\"", ctx->values.PAPP, ctx->values.IINST, ctx->values.HHPHC ? ctx->values.HHPHC:' ', ctx->values.PTEC);
					if(ctx->values.HCHC){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"HCHC\": %d,\n\"HCHP\": %d", ctx->values.HCHC, ctx->values.HCHP);
					}
					if(ctx->values.BASE){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"BASE\": %d", ctx->values.BASE);
					}
					strcat(l,"\n}\n");

					papub( ctx->sumtopic, strlen(l), l, 1 );
				}

				if(debug){
					time_t t;
					char buf[26];

					time( &t );
					printf("*d* New frame %s : %s", ctx->topic, ctime_r( &t, buf));	/* ctime_r in not in C99 standard but is safer in multi-threaded environment */
				}

				if( cfg.delay )	/* existing only if we have to wait */
					break;
			} else if((arg = striKWcmp(l,"PAPP"))){
				ctx->values.PAPP = atoi(extr_arg(arg,5));

				if(cfg.period && ctx->max.PAPP < ctx->values.PAPP )
						ctx->max.PAPP = ctx->values.PAPP;

				if(debug)
					printf("*d* Power : '%d'\n", ctx->values.PAPP);
				sprintf(l, "%s/values/PAPP", ctx->topic);
				sprintf(val, "%d", ctx->values.PAPP);
				papub( l, strlen(val), val, 0 );
			} else if((arg = striKWcmp(l,"IINST"))){
				ctx->values.IINST = atoi(extr_arg(arg,3));

				if(cfg.period && ctx->max.IINST < ctx->values.IINST )
						ctx->max.IINST = ctx->values.IINST;

				if(debug)
					printf("*d* Intensity : '%d'\n", ctx->values.IINST);
				sprintf(l, "%s/values/IINST", ctx->topic);
				sprintf(val, "%d", ctx->values.IINST);
				papub( l, strlen(val), val, 0 );
			} else if((arg = striKWcmp(l,"HCHC"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.HCHC != v){
					int diff = v - ctx->values.HCHC;
					sprintf(l, "%s/values/HCHC", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.HCHC){	/* forget the 1st run */
						if(debug)
							printf("*d* Cnt HC : '%d'\n", diff);
						sprintf(l, "%s/values/HCHCd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.HCHC < diff )
							ctx->max.HCHC = diff;
						papub( l, strlen(val), val, 0 );

					}
					ctx->values.HCHC = v;
				}
			} else if((arg = striKWcmp(l,"HCHP"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.HCHP != v){
					int diff = v - ctx->values.HCHP;
					sprintf(l, "%s/values/HCHP", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.HCHP){
						if(debug)
							printf("*d* Cnt HP : '%d'\n", diff);
						sprintf(l, "%s/values/HCHPd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.HCHP < diff)
							ctx->max.HCHP = diff;

						papub( l, strlen(val), val, 0 );

					}
					ctx->values.HCHP = v;
				}
			} else if((arg = striKWcmp(l,"BASE"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.BASE != v){
					int diff = v - ctx->values.BASE;
					sprintf(l, "%s/values/BASE", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.BASE){
						if(debug)
							printf("*d* Cnt BASE : '%d'\n", diff);
						sprintf(l, "%s/values/BASEd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.BASE < diff)
							ctx->max.BASE = diff;

						papub( l, strlen(val), val, 0 );

					}
					ctx->values.BASE = v;
				}
			} else if((arg = striKWcmp(l,"PTEC"))){
				arg = extr_arg(arg, 4);
				if(strncmp(ctx->values.PTEC, arg, 4)){
					memcpy(ctx->values.PTEC, arg, 4);
					sprintf(val, "%.4s", arg);
					if(debug)
						printf("*d* PTEC : '%s'\n", val);
					sprintf(l, "%s/values/PTEC", ctx->topic);

					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"ISOUSC"))){
				int v = atoi(extr_arg(arg,2));
				if(ctx->values.ISOUSC != v){
					ctx->values.ISOUSC = v;
					sprintf(l, "%s/values/ISOUSC", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"HHPHC"))){
				char v = *extr_arg(arg,1);
				if(ctx->values.HHPHC != v){
					ctx->values.HHPHC = v;
					sprintf(l, "%s/values/HHPHC", ctx->topic);
					sprintf(val, "%c", v);
					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"OPTARIF"))){
				arg = extr_arg(arg, 4);
				if(strncmp(ctx->values.OPTARIF, arg, 4)){
					memcpy(ctx->values.OPTARIF, arg, 4);
					sprintf(val, "%4s", arg);
					if(debug)
						printf("*d* OPTARIF : '%s'\n", val);
					sprintf(l, "%s/values/OPTARIF", ctx->topic);

					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"IMAX"))){
				int v = atoi(extr_arg(arg,3));
				if(ctx->values.IMAX != v){
					ctx->values.IMAX = v;
					sprintf(l, "%s/values/IMAX", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"MOTD"))){	/* nothing to do, only to avoid uneeded message if debug is enabled */
			} else if(debug)
				printf(">>> Ignored : '%.4s'\n", l);
		}
		if(feof(ftrame)){	/* Stream finished, we have to leave */
			fclose(ftrame);
			break;
		}
		fclose(ftrame);


		sleep( cfg.delay );
	}

	pthread_exit(0);
}

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
				puts("TeleInfod (c) L.Faillie 2015-17");
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

	if(!cfg.sections){
		fputs("*F* No section defined : giving up ...\n", stderr);
		exit(EXIT_FAILURE);
	}

#ifdef USE_MOSQUITTO
	mosquitto_lib_init();
	if(!(cfg.mosq = mosquitto_new(
		"TeleInfod",	/* Id for this client */
		true,			/* clean msg on exit */
		NULL			/* No call backs */
	))){
		perror("Moquitto_new()");
		mosquitto_lib_cleanup();
		exit(EXIT_FAILURE);
	}

	switch( mosquitto_connect(cfg.mosq, cfg.Broker_Host, cfg.Broker_Port, BRK_KEEPALIVE) ){
	case MOSQ_ERR_INVAL:
		fputs("Invalid parameter for mosquitto_connect()\n", stderr);
		mosquitto_destroy(cfg.mosq);
		mosquitto_lib_cleanup();
		exit(EXIT_FAILURE);
	case MOSQ_ERR_ERRNO:
		perror("mosquitto_connect()");
		mosquitto_destroy(cfg.mosq);
		mosquitto_lib_cleanup();
		exit(EXIT_FAILURE);
	default :
		if(debug)
			puts("Connected using Mosquitto library");
	}
#elif defined(USE_PAHO)
	{
		MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
		conn_opts.reliable = 0;

		MQTTClient_create( &cfg.client, cfg.Broker_Host, "TeleInfod", MQTTCLIENT_PERSISTENCE_NONE, NULL);
		MQTTClient_setCallbacks( cfg.client, NULL, connlost, msgarrived, NULL);

		switch( MQTTClient_connect( cfg.client, &conn_opts) ){
		case MQTTCLIENT_SUCCESS : 
			break;
		case 1 : fputs("Unable to connect : Unacceptable protocol version\n", stderr);
			exit(EXIT_FAILURE);
		case 2 : fputs("Unable to connect : Identifier rejected\n", stderr);
			exit(EXIT_FAILURE);
		case 3 : fputs("Unable to connect : Server unavailable\n", stderr);
			exit(EXIT_FAILURE);
		case 4 : fputs("Unable to connect : Bad user name or password\n", stderr);
			exit(EXIT_FAILURE);
		case 5 : fputs("Unable to connect : Not authorized\n", stderr);
			exit(EXIT_FAILURE);
		default :
			fputs("Unable to connect : Unknown version\n", stderr);
			exit(EXIT_FAILURE);
		}
	}
#endif

	atexit(theend);

		/* Creation of reading threads */
	assert(!pthread_attr_init (&thread_attr));
	assert(!pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED));
	for(struct CSection *s = cfg.sections; s; s = s->next){
		if(pthread_create( &(s->thread), &thread_attr, process_flow, s) < 0){
			fputs("*F* Can't create a processing thread\n", stderr);
			exit(EXIT_FAILURE);
		}
	}

	signal(SIGINT, handleInt);
	if(cfg.period){
		char l[MAXLINE];
		for(;;){
			sleep( cfg.period );
			for(struct CSection *ctx = cfg.sections; ctx; ctx = ctx->next){
				if(ctx->sumtopic){
					sprintf(l, "{\n\"PAPP\": %d,\n\"IINST\": %d,\n\"HHPHC\": \"%c\",\n\"PTEC\": \"%.4s\"", ctx->values.PAPP, ctx->values.IINST, ctx->values.HHPHC ? ctx->values.HHPHC:' ', ctx->values.PTEC);
					ctx->max.PAPP = ctx->max.IINST = -1;

					if(ctx->values.HCHC){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"HCHC\": %d,\n\"HCHP\": %d", ctx->values.HCHC, ctx->values.HCHP);
					}
					if(ctx->values.BASE){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"BASE\": %d", ctx->values.BASE);
					}

					if(ctx->max.HCHC != -1){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"HCHCd\": %d", ctx->max.HCHC);
						ctx->max.HCHC = -1;
					}
					if(ctx->max.HCHP != -1){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"HCHPd\": %d", ctx->max.HCHP);
						ctx->max.HCHP = -1;
					}
					if(ctx->max.BASE != -1){
						char *t = l + strlen(l);
						sprintf(t, ",\n\"BASEd\": %d", ctx->max.BASE);
						ctx->max.BASE = -1;
					}
					strcat(l,"\n}\n");

					papub( ctx->sumtopic, strlen(l), l, 1 );
				}
			}
		}
	} else
		pause();

	exit(EXIT_SUCCESS);
}

