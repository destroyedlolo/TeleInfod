/*
 * TeleInfod
 * 	A daemon to publish EDF's "Télé Information" to a MQTT broker
 *
 * 	Compilation
 *
gcc -std=c99 -lpthread -lmosquitto -Wall TeleInfod.c -o TeleInfod
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
 *		03/05/2015 - v0.1 LF - Start of development
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

#include <mosquitto.h>

#define VERSION "0.1"
#define DEFAULT_CONFIGURATION_FILE "/usr/local/etc/TeleInfod.conf"
#define MAXLINE 1024	/* Maximum length of a line to be read */
#define BRK_KEEPALIVE 60	/* Keep alive signal to the broker */

bool debug = 0;

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
struct CSection {	/* Section of the configuration : a TéléInfo flow */
	struct CSection *next;	/* Next section */
	pthread_t thread;
	const char *port;		/* Where to read */
	const char *topic;		/* Broker's topic */
	int PAPP;				/* power */
	int IINST;				/* Intensity */
	int HCHC;				/* Counter "Heure Creuse" */
	int HCHP;				/* Counter "Heure Plaine" */
	int BASE;				/* Counter "Base" */
};

struct Config {
	struct CSection *sections;
	const char *Broker_Host;
	int Broker_Port;
	int delay;
	struct mosquitto *mosq;
} cfg;

void read_configuration( const char *fch){
	FILE *f;
	char l[MAXLINE];
	char *arg;

	cfg.sections = NULL;
	cfg.Broker_Host = "localhost";
	cfg.Broker_Port = 1883;
	cfg.delay = 30;
	cfg.mosq = NULL;

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
			n->next = cfg.sections;

			n->port = n->topic = NULL;

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
			cfg.Broker_Port = atoi( arg );
			if(debug)
				printf("Broker port : %d\n", cfg.Broker_Port);
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

	/*
	 * Processing
	 */
void *process_flow(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *ftrame;
	char l[MAXLINE];
	char *arg;

	if(!ctx->topic){
		fputs("*E* configuration error : no topic specified, ignoring this section\n", stderr);
		pthread_exit(0);
	}
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

		while(fgets(l, MAXLINE, ftrame))	/* Wait 'till the beginning of the trame */
			if(!strncmp(l,"ADCO",4))
				break;
		if(feof(ftrame)){
			fclose(ftrame);
			break;
		}

		while(fgets(l, MAXLINE, ftrame)){	/* Read payloads */
			if(!strncmp(l,"ADCO",4))	/* Reaching the next one : existing */
				break;
			else if((arg = striKWcmp(l,"PAPP"))){
				ctx->PAPP = atoi(extr_arg(arg,5));
				if(debug)
					printf("Power : '%d'\n", ctx->PAPP);
				sprintf(l, "%s/PAPP", ctx->topic);
				mosquitto_publish(cfg.mosq, NULL, l, sizeof(ctx->PAPP), &(ctx->PAPP), 0, false);
			} else if((arg = striKWcmp(l,"IINST"))){
				ctx->IINST = atoi(extr_arg(arg,3));
				if(debug)
					printf("Intensity : '%d'\n", ctx->IINST);
				sprintf(l, "%s/IINST", ctx->topic);
				mosquitto_publish(cfg.mosq, NULL, l, sizeof(ctx->IINST), &(ctx->IINST), 0, false);
			} else if((arg = striKWcmp(l,"HCHC"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->HCHC != v){
					int diff = v - ctx->HCHC;
					sprintf(l, "%s/HCHC", ctx->topic);
					mosquitto_publish(cfg.mosq, NULL, l, sizeof(v), &v, 0, false);
					if(ctx->HCHC){	/* forget the 1st run */
						if(debug)
							printf("Cnt HC : '%d'\n", diff);
						sprintf(l, "%s/HCHCd", ctx->topic);
						mosquitto_publish(cfg.mosq, NULL, l, sizeof(diff), &diff, 0, false);
					}
					ctx->HCHC = v;
				}
			} else if((arg = striKWcmp(l,"HCHP"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->HCHP != v){
					int diff = v - ctx->HCHP;
					sprintf(l, "%s/HCHP", ctx->topic);
					mosquitto_publish(cfg.mosq, NULL, l, sizeof(v), &v, 0, false);
					if(ctx->HCHP){
						if(debug)
							printf("Cnt HP : '%d'\n", diff);
						sprintf(l, "%s/HCHPd", ctx->topic);
						mosquitto_publish(cfg.mosq, NULL, l, sizeof(diff), &diff, 0, false);
					}
					ctx->HCHP = v;
				}
			} else if((arg = striKWcmp(l,"BASE"))){
				int v = atoi(extr_arg(arg,9));
				if(ctx->BASE != v){
					int diff = v - ctx->BASE;
					sprintf(l, "%s/BASE", ctx->topic);
					mosquitto_publish(cfg.mosq, NULL, l, sizeof(v), &v, 0, false);
					if(ctx->BASE){
						if(debug)
							printf("Cnt BASE : '%d'\n", diff);
						sprintf(l, "%s/BASEd", ctx->topic);
						mosquitto_publish(cfg.mosq, NULL, l, sizeof(diff), &diff, 0, false);
					}
					ctx->BASE = v;
				}
			}
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
					"\t-f<file> : read <file> for configuration\n"
					"\t\t(default is '%s')\n",
					basename(av[0]), VERSION, DEFAULT_CONFIGURATION_FILE
				);
				exit(EXIT_FAILURE);
			} else if(!strcmp(av[i], "-d")){
				debug = 1;
				puts("TeleInfod (c) L.Faillie 2015");
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
	}

		/* Creation of reading threads */
	assert(!pthread_attr_init (&thread_attr));
	assert(!pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED));
	for(struct CSection *s = cfg.sections; s; s = s->next){
		if(pthread_create( &(s->thread), &thread_attr, process_flow, s) < 0){
			fputs("*F* Can't create a processing thread\n", stderr);
			exit(EXIT_FAILURE);
		}
	}

sleep(400);

		/* Some cleanup */
	mosquitto_destroy(cfg.mosq);
	mosquitto_lib_cleanup();

	exit(EXIT_SUCCESS);
}

