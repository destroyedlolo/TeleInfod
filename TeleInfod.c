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
		int SMAXSNDT;		/* Horodatage */
		int SMAXSN1;		/* Puissance app max. soutirée n-1 */
		int SMAXSN1DT;		/* Horodatage */
		int SINSTI;			/* Puissance app. Instantanée injectée */
		int SMAXIN;			/* Puissance app. max. injectée n */
		int SMAXINDT;		/* Horodatage */
		int SMAXIN1;		/* Puissance app max. injectée n-1 */
		int SMAXIN1DT;		/* Horodatage */
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
	char *sumtopic;			/* Summary topic */
	const char *cctopic;	/* Converted Customer topic */
	char *ccsumtopic;	/* Summary topic */
	const char *cptopic;	/* Converted Producer topic */
	char *cpsumtopic;	/* Summary topic */
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

void publish_sum_historic( char *l, struct CSection *ctx ){
	if(ctx->sumtopic){
		sprintf(l, "{\n\"PAPP\": %d,\n\"IINST\": %d,\n\"HHPHC\": \"%c\",\n\"PTEC\": \"%.4s\"",
			ctx->values.historic.PAPP,
			ctx->values.historic.IINST,
			ctx->values.historic.HHPHC ? ctx->values.historic.HHPHC:' ',
			ctx->values.historic.PTEC
		);

		if(ctx->values.historic.HCHC){
			char *t = l + strlen(l);
			sprintf(t, ",\n\"HCHC\": %d,\n\"HCHP\": %d", 
				ctx->values.historic.HCHC,
				ctx->values.historic.HCHP
			);
		}

		if(ctx->values.historic.BASE){
			char *t = l + strlen(l);
			sprintf(t, ",\n\"BASE\": %d", ctx->values.historic.BASE);
		}

		strcat(l,"\n}\n");
		papub( ctx->sumtopic, strlen(l), l, 1 );
	}
}

void *process_historic(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *ftrame;
	char l[MAXLINE];
	char *arg;
	char val[12];

	assert( (ctx->sumtopic = malloc( strlen(ctx->topic)+9 )) );	/* + "/summary" + 1 */
	strcpy( ctx->sumtopic, ctx->topic );
	strcat( ctx->sumtopic, "/summary" );

	if(debug)
		printf("Launching a processing historic for '%s'\n", ctx->name);

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
			printf("*d* Let's go with '%s'\n", ctx->port);

		while(fgets(l, MAXLINE, ftrame)){	/* Read payloads */
			if(debug > 1)
				printf(l);
			if(!strncmp(l,"ADCO",4)){ /* Reaching the next one */
					/* publish summary */
				if(!cfg.period)	/* No period specified, sending actual values */
					publish_sum_historic( l, ctx );

				if(debug){
					time_t t;
					char buf[26];

					time( &t );
					printf("*d* New frame %s : %s", ctx->name, ctime_r( &t, buf));	/* ctime_r in not in C99 standard but is safer in multi-threaded environment */
				}

				if( cfg.delay )	/* existing only if a deplay b/w frame is in place */
					break;
			} else if((arg = striKWcmp(l,"PAPP"))){
				ctx->values.historic.PAPP = atoi(extr_arg(arg,5));

				if(cfg.period && ctx->max.historic.PAPP < ctx->values.historic.PAPP )
						ctx->max.historic.PAPP = ctx->values.historic.PAPP;

				if(debug)
					printf("*d* Power : '%d'\n", ctx->values.historic.PAPP);
				sprintf(l, "%s/values/PAPP", ctx->topic);
				sprintf(val, "%d", ctx->values.historic.PAPP);
				papub( l, strlen(val), val, 0 );
			} else if((arg = striKWcmp(l,"IINST"))){
				ctx->values.historic.IINST = atoi(extr_arg(arg,3));

				if(cfg.period && ctx->max.historic.IINST < ctx->values.historic.IINST )
						ctx->max.historic.IINST = ctx->values.historic.IINST;

				if(debug)
					printf("*d* Intensity : '%d'\n", ctx->values.historic.IINST);

				sprintf(l, "%s/values/IINST", ctx->topic);
				sprintf(val, "%d", ctx->values.historic.IINST);
				papub( l, strlen(val), val, 0 );
			} else if((arg = striKWcmp(l,"HCHC"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.historic.HCHC != v){
					int diff = v - ctx->values.historic.HCHC;
					sprintf(l, "%s/values/HCHC", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.historic.HCHC){	/* forget the 1st run */
						if(debug)
							printf("*d* Cnt HC : '%d'\n", diff);
						sprintf(l, "%s/values/HCHCd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.historic.HCHC < diff )
							ctx->max.historic.HCHC = diff;
						papub( l, strlen(val), val, 0 );

					}
					ctx->values.historic.HCHC = v;
				}
			} else if((arg = striKWcmp(l,"HCHP"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.historic.HCHP != v){
					int diff = v - ctx->values.historic.HCHP;
					sprintf(l, "%s/values/HCHP", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.historic.HCHP){
						if(debug)
							printf("*d* Cnt HP : '%d'\n", diff);
						sprintf(l, "%s/values/HCHPd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.historic.HCHP < diff)
							ctx->max.historic.HCHP = diff;

						papub( l, strlen(val), val, 0 );

					}
					ctx->values.historic.HCHP = v;
				}
			} else if((arg = striKWcmp(l,"BASE"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->values.historic.BASE != v){
					int diff = v - ctx->values.historic.BASE;
					sprintf(l, "%s/values/BASE", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 0 );

					if(ctx->values.historic.BASE){
						if(debug)
							printf("*d* Cnt BASE : '%d'\n", diff);

						sprintf(l, "%s/values/BASEd", ctx->topic);
						sprintf(val, "%d", diff);

						if(cfg.period && ctx->max.historic.BASE < diff)
							ctx->max.historic.BASE = diff;

						papub( l, strlen(val), val, 0 );

					}
					ctx->values.historic.BASE = v;
				}
			} else if((arg = striKWcmp(l,"PTEC"))){
				arg = extr_arg(arg, 4);
				if(strncmp(ctx->values.historic.PTEC, arg, 4)){
					memcpy(ctx->values.historic.PTEC, arg, 4);
					sprintf(val, "%.4s", arg);
					if(debug)
						printf("*d* PTEC : '%s'\n", val);
					sprintf(l, "%s/values/PTEC", ctx->topic);

					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"ISOUSC"))){
				int v = atoi(extr_arg(arg,2));
				if(ctx->values.historic.ISOUSC != v){
					ctx->values.historic.ISOUSC = v;
					sprintf(l, "%s/values/ISOUSC", ctx->topic);
					sprintf(val, "%d", v);
					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"HHPHC"))){
				char v = *extr_arg(arg,1);
				if(ctx->values.historic.HHPHC != v){
					ctx->values.historic.HHPHC = v;
					sprintf(l, "%s/values/HHPHC", ctx->topic);
					sprintf(val, "%c", v);
					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"OPTARIF"))){
				arg = extr_arg(arg, 4);
				if(strncmp(ctx->values.historic.OPTARIF, arg, 4)){
					memcpy(ctx->values.historic.OPTARIF, arg, 4);
					sprintf(val, "%4s", arg);
					if(debug)
						printf("*d* OPTARIF : '%s'\n", val);
					sprintf(l, "%s/values/OPTARIF", ctx->topic);

					papub( l, strlen(val), val, 1 );
				}
			} else if((arg = striKWcmp(l,"IMAX"))){
				int v = atoi(extr_arg(arg,3));
				if(ctx->values.historic.IMAX != v){
					ctx->values.historic.IMAX = v;
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

			/* Reaching here if deplay in place */
		fclose(ftrame);
		sleep( cfg.delay );
	}

	pthread_exit(0);
}

void publish_sum_standard( char *l, struct CSection *ctx ){
	if(ctx->sumtopic){
		sprintf(l, "{\n\"EAST\": %d,\n\"EAIT\": %d,\n\"IRMS1\": %d,\n\"SINSTS\": %d,\n\"SINSTI\": %d,\n\"UMOY1\": %d,\n\"RELAIS\": %d\n}\n",
			ctx->values.standard.EAST,
			ctx->values.standard.EAIT,
			ctx->values.standard.IRMS1,
			ctx->values.standard.SINSTS,
			ctx->values.standard.SINSTI,
			ctx->values.standard.UMOY1,
			ctx->values.standard.RELAIS
		);

		papub( ctx->sumtopic, strlen(l), l, 1 );
	}

	if(ctx->cpsumtopic){
		sprintf(l, "{\n\"PAPP\": %d,\n\"IINST\": %d,\n\"HHPHC\": \" \",\n\"PTEC\": \"PROD\",\n\"BASE\": %d\n}\n",
			ctx->values.standard.SINSTI,
			ctx->values.standard.IRMS1,
			ctx->values.standard.EAIT
		);

		papub( ctx->cpsumtopic, strlen(l), l, 1 );
	}
}

void *process_standard(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *ftrame;
	char l[MAXLINE];
	char *arg;
	char val[12];

	if( ctx->topic ){
		assert( (ctx->sumtopic = malloc( strlen(ctx->topic)+9 )) );	/* + "/summary" + 1 */
		strcpy( ctx->sumtopic, ctx->topic );
		strcat( ctx->sumtopic, "/summary" );
	}

	if( ctx->cctopic ){
		assert( (ctx->ccsumtopic = malloc( strlen(ctx->cctopic)+9 )) );	/* + "/summary" + 1 */
		strcpy( ctx->ccsumtopic, ctx->cctopic );
		strcat( ctx->ccsumtopic, "/summary" );
	}

	if( ctx->cptopic ){
		assert( (ctx->cpsumtopic = malloc( strlen(ctx->cptopic)+9 )) );	/* + "/summary" + 1 */
		strcpy( ctx->cpsumtopic, ctx->cptopic );
		strcat( ctx->cpsumtopic, "/summary" );
	}

	if(debug)
		printf("Launching a processing standard for '%s'\n", ctx->name);

	for(;;){
		if(!(ftrame = fopen( ctx->port, "r" ))){
			perror(ctx->port);
			exit(EXIT_FAILURE);
		}

		if(debug)
			printf("*d* Waiting for beginning of a frame for '%s'\n", ctx->port);

		while(fgets(l, MAXLINE, ftrame))	/* Wait 'till the beginning of the trame */
			if(!strncmp(l,"ADSC",4))
				break;
		if(feof(ftrame)){
			fclose(ftrame);
			break;
		}

		if(debug)
			printf("*d* Let's go with '%s'\n", ctx->port);

		while(fgets(l, MAXLINE, ftrame)){	/* Read payloads */
			if(debug > 1)
				printf(l);

			if((arg = striKWcmp(l,"ADSC"))){	/* New frame */
				if(debug){
					time_t t;
					char buf[26];

					time( &t );
					printf("*d* New frame %s : %s", ctx->name, ctime_r( &t, buf));
				}

					/* Send summarry */
				if(!cfg.period)	/* No period specified, sending actual values */
					publish_sum_standard( l, ctx );

					/* existing only if a deplay b/w frame is in place */
				if( cfg.delay )
					break;

					/*******
					 * Counters
					 *******/

 			} else if((arg = striKWcmp(l,"EAST"))){
				int v = atoi(extr_arg(arg,9));

				if( ctx->values.standard.EAST != v ){	/* Counter changed */
					int diff = v - ctx->values.standard.EAST;
					ctx->values.standard.EAST = v;

					if(debug)
						printf("*d* Idx Energie Soutirée (EAST) : %d (%d)\n", v, diff);

					if( ctx->topic ){	/* Sending main topic */
						sprintf(l, "%s/values/EAST", ctx->topic);
						sprintf(val, "%d", v);
						papub( l, strlen(val), val, 0 );
			
						if( diff ){
							sprintf(l, "%s/values/EASTd", ctx->topic);
							sprintf(val, "%d", diff);
							papub( l, strlen(val), val, 0 );
						}
					}

#if 0
					if( ctx->cctopic ){	/* Converted for consummer */
						sprintf(l, "%s/values/BASE", ctx->cctopic);
						sprintf(val, "%d", v);
						papub( l, strlen(val), val, 0 );
					
						if( diff ){
							sprintf(l, "%s/values/BASEd", ctx->cctopic);
							sprintf(val, "%d", diff);
							papub( l, strlen(val), val, 0 );
						}
					}
#endif
				}
 			} else if((arg = striKWcmp(l,"EAIT"))){
				int v = atoi(extr_arg(arg,9));

				if( ctx->values.standard.EAIT != v ){	/* Counter changed */
					int diff = v - ctx->values.standard.EAIT;
					ctx->values.standard.EAIT = v;

					if(debug)
						printf("*d* Idx Energie Injectée (EAIT) : %d (%d)\n", v, diff);

					if( ctx->topic ){	/* Sending main topic */
						sprintf(l, "%s/values/EAIT", ctx->topic);
						sprintf(val, "%d", v);
						papub( l, strlen(val), val, 0 );
			
						if( diff ){
							sprintf(l, "%s/values/EAITd", ctx->topic);
							sprintf(val, "%d", diff);
							papub( l, strlen(val), val, 0 );
						}
					}

					if( ctx->cptopic ){	/* Converted for producer */
						sprintf(l, "%s/values/BASE", ctx->cptopic);
						sprintf(val, "%d", v);
						papub( l, strlen(val), val, 0 );
					
						if( diff ){
							sprintf(l, "%s/values/BASEd", ctx->cptopic);
							sprintf(val, "%d", diff);
							papub( l, strlen(val), val, 0 );
						}
					}
				}

 			} else if((arg = striKWcmp(l,"IRMS1"))){
				ctx->values.standard.IRMS1 = atoi(extr_arg(arg,3));

				if(cfg.period && ctx->max.standard.IRMS1 < ctx->values.standard.IRMS1 )
						ctx->max.standard.IRMS1 = ctx->values.standard.IRMS1;

				if(debug)
					printf("*d* Intensity : '%d'\n", ctx->values.standard.IRMS1);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/IRMS1", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.IRMS1);
					papub( l, strlen(val), val, 0 );
				}

				if( ctx->cptopic ){	/* Sending main topic */
					sprintf(l, "%s/values/IINST", ctx->cptopic);
					sprintf(val, "%d", ctx->values.standard.IRMS1);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"PREF"))){
				ctx->values.standard.PREF = atoi(extr_arg(arg,2));

				if(cfg.period && ctx->max.standard.PREF < ctx->values.standard.PREF )
						ctx->max.standard.PREF = ctx->values.standard.PREF;

				if(debug)
					printf("*d* Puissance ref : '%d'\n", ctx->values.standard.PREF);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/PREF", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.PREF);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"PCOUP"))){
				ctx->values.standard.PCOUP = atoi(extr_arg(arg,2));

				if(cfg.period && ctx->max.standard.PCOUP < ctx->values.standard.PCOUP )
						ctx->max.standard.PCOUP = ctx->values.standard.PCOUP;

				if(debug)
					printf("*d* Puissance coupure : '%d'\n", ctx->values.standard.PCOUP);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/PCOUP", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.PCOUP);
					papub( l, strlen(val), val, 0 );
				}

				if( ctx->cptopic ){	/* Sending main topic */
					sprintf(l, "%s/values/ISOUSC", ctx->cptopic);
					sprintf(val, "%d", ctx->values.standard.IRMS1 / 200);
					papub( l, strlen(val), val, 0 );
				}
	
 			} else if((arg = striKWcmp(l,"SINSTS"))){
				ctx->values.standard.SINSTS = atoi(extr_arg(arg,5));

				if(cfg.period && ctx->max.standard.SINSTS < ctx->values.standard.SINSTS )
						ctx->max.standard.SINSTS = ctx->values.standard.SINSTS;
	
				if(debug)
					printf("*d* Energie Soutirée (SINSTS) : %d\n", ctx->values.standard.SINSTS);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SINSTS", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SINSTS);
					papub( l, strlen(val), val, 0 );
				}

#if 0
				if( ctx->cctopic ){	/* Converted for consummer */
					sprintf(l, "%s/values/PAPP", ctx->cctopic);
					sprintf(val, "%d", ctx->values.standard.SINSTS);
					papub( l, strlen(val), val, 0 );
				}
#endif
 			} else if((arg = striKWcmp(l,"SINSTI"))){
				ctx->values.standard.SINSTI = atoi(extr_arg(arg,5));

				if(cfg.period && ctx->max.standard.SINSTI < ctx->values.standard.SINSTI )
						ctx->max.standard.SINSTI = ctx->values.standard.SINSTI;
	
				if(debug)
					printf("*d* Energie Injecte (SINSTI) : %d\n", ctx->values.standard.SINSTI);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SINSTI", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SINSTI);
					papub( l, strlen(val), val, 0 );
				}

				if( ctx->cptopic ){	/* Converted for consummer */
					sprintf(l, "%s/values/PAPP", ctx->cptopic);
					sprintf(val, "%d", ctx->values.standard.SINSTI);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"SMAXSN-1"))){
				ctx->values.standard.SMAXSN1DT = atoi(extr_arg(arg, 11)+1);
				ctx->values.standard.SMAXSN1 = atoi(extr_arg(arg + 14,5));

				if(cfg.period && ctx->max.standard.SMAXSN1 < ctx->values.standard.SMAXSN1 )
						ctx->max.standard.SMAXSN1 = ctx->values.standard.SMAXSN1;

				if(debug)
					printf("*d* Puissance max. soutirée (j-1) : '%d' %d\n", ctx->values.standard.SMAXSN1, ctx->values.standard.SMAXSN1DT);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SMAXSN1", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXSN1);
					papub( l, strlen(val), val, 0 );

					sprintf(l, "%s/values/SMAXSN1DT", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXSN1DT);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"SMAXSN"))){
				ctx->values.standard.SMAXSNDT = atoi(extr_arg(arg, 11)+1);
				ctx->values.standard.SMAXSN = atoi(extr_arg(arg + 14,5));

				if(cfg.period && ctx->max.standard.SMAXSN < ctx->values.standard.SMAXSN )
						ctx->max.standard.SMAXSN = ctx->values.standard.SMAXSN;

				if(debug)
					printf("*d* Puissance max. soutirée : '%d' %d\n", ctx->values.standard.SMAXSN, ctx->values.standard.SMAXSNDT);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SMAXSN", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXSN);
					papub( l, strlen(val), val, 0 );

					sprintf(l, "%s/values/SMAXSNDT", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXSNDT);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"SMAXIN-1"))){
				ctx->values.standard.SMAXIN1DT = atoi(extr_arg(arg, 11)+1);
				ctx->values.standard.SMAXIN1 = atoi(extr_arg(arg + 14,5));

				if(cfg.period && ctx->max.standard.SMAXIN1 < ctx->values.standard.SMAXIN1 )
						ctx->max.standard.SMAXIN1 = ctx->values.standard.SMAXIN1;

				if(debug)
					printf("*d* Puissance max. injectée (j-1) : '%d' %d\n", ctx->values.standard.SMAXIN1, ctx->values.standard.SMAXIN1DT);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SMAXIN1", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXIN1);
					papub( l, strlen(val), val, 0 );

					sprintf(l, "%s/values/SMAXIN1DT", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXIN1DT);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"SMAXIN"))){
				ctx->values.standard.SMAXINDT = atoi(extr_arg(arg, 11)+1);
				ctx->values.standard.SMAXIN = atoi(extr_arg(arg + 14,5));

				if(cfg.period && ctx->max.standard.SMAXIN < ctx->values.standard.SMAXIN )
						ctx->max.standard.SMAXIN = ctx->values.standard.SMAXIN;

				if(debug)
					printf("*d* Puissance max. injectée : '%d' %d\n", ctx->values.standard.SMAXIN, ctx->values.standard.SMAXINDT);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/SMAXIN", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXIN);
					papub( l, strlen(val), val, 0 );

					sprintf(l, "%s/values/SMAXINDT", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.SMAXINDT);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"UMOY1"))){
				ctx->values.standard.UMOY1 = atoi(extr_arg(arg + 14,3));

				if(cfg.period && ctx->max.standard.UMOY1 < ctx->values.standard.UMOY1 )
						ctx->max.standard.UMOY1 = ctx->values.standard.UMOY1;

				if(debug)
					printf("*d* Tension moy. ph. 1 : '%d'\n", ctx->values.standard.UMOY1);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/UMOY1", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.UMOY1);
					papub( l, strlen(val), val, 0 );
				}

 			} else if((arg = striKWcmp(l,"RELAIS"))){
				ctx->values.standard.RELAIS = atoi(extr_arg(arg,3));

				if(cfg.period && ctx->max.standard.RELAIS < ctx->values.standard.RELAIS )
						ctx->max.standard.RELAIS = ctx->values.standard.RELAIS;

				if(debug)
					printf("*d* Relais : '%d'\n", ctx->values.standard.RELAIS);

				if( ctx->topic ){	/* Sending main topic */
					sprintf(l, "%s/values/RELAIS", ctx->topic);
					sprintf(val, "%d", ctx->values.standard.RELAIS);
					papub( l, strlen(val), val, 0 );
				}

			}
		}

		if(feof(ftrame)){	/* Stream finished, we have to leave */
			fclose(ftrame);
			break;
		}

			/* Reaching here if deplay in place */
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

	if(debug){
		printf("Sanity checks : ");
		fflush( stdout );
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

	if(debug)
		puts("PASSED\n");

		/* Connecting to the broker */
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
	for( s = cfg.sections; s; s = s->next ){
		if( s->standard ){
			if(pthread_create( &(s->thread), &thread_attr, process_standard, s) < 0){
				fputs("*F* Can't create a processing thread\n", stderr);
				exit(EXIT_FAILURE);
			}
		} else {
			if(pthread_create( &(s->thread), &thread_attr, process_historic, s) < 0){
				fputs("*F* Can't create a processing thread\n", stderr);
				exit(EXIT_FAILURE);
			}
		}
	}

		/* Lets threads working */
	signal(SIGINT, handleInt);
	if(cfg.period){
		char l[MAXLINE];
		for(;;){
			sleep( cfg.period );

			for(struct CSection *ctx = cfg.sections; ctx; ctx = ctx->next){
				if(ctx->standard)
					publish_sum_standard( l, ctx );
				else 	/* Historic mode */
					publish_sum_historic( l, ctx );
			}
		}
	}

	pause();	/* No summary to send : waiting for the end */

	exit(EXIT_SUCCESS);
}

