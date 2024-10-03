/*
 * TeleInfod
 * 	A daemon to publish EDF's "Télé Information" to a MQTT broker
 *
 * Copyright 2015-2023 Laurent Faillie (destroyedlolo)
 *
 *	TeleInfod is covered by 
 *	Creative Commons Attribution-NonCommercial 3.0 License
 *	(http://creativecommons.org/licenses/by-nc/3.0/) 
 *	Consequently, you're free to use if for personal or non-profit usage,
 *	professional or commercial usage REQUIRES a commercial licence.
 *
 *	TeleInfod is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#ifdef USE_MOSQUITTO
#	include <mosquitto.h>
#elif defined(USE_PAHO)
#	include <MQTTClient.h>
#endif

#include "Version.h"
#include "Config.h"

unsigned int debug = 0;
const char *Broker_Host;
int Broker_Port;
struct CSection *sections;

#ifdef USE_MOSQUITTO
struct mosquitto *mosq;
#elif defined(USE_PAHO)
MQTTClient client;
#else
#	error "No MQTT library defined"
#endif

	/* **
	 * Helpers
	 * **/
char *removeLF(char *s){
	size_t l=strlen(s);
	if(l && s[--l] == '\n')
		s[l] = 0;
	return s;
}

char *striKWcmp( char *s, const char *kw ){
/* compares string s against kw
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
/* Extracts an argument from TéléInfo trame 
 *	s : argument string just after the token
 *	l : length of the argument
 */
	s++;	/* Skip the leading space */
	s[l]=0;
	return s;
}


	/* **
	 * Fill configuration from given configuration file
	 * -> fch : configuration file to read
	 * **/
static void read_configuration(const char *fch){
	FILE *f;
	char l[MAXLINE];
	char *arg;
	unsigned int ln = 0;

		/* default configuration */
#ifdef USE_PAHO
	Broker_Host = "tcp://localhost:1883";
	client = NULL;
#else
	Broker_Host = "localhost";
	Broker_Port = 1883;
	mosq = NULL;
#endif

	if(debug)
		printf("Reading configuration file '%s'\n", fch);

		/* Reading the configuration file */
	if(!(f=fopen(fch, "r"))){
		perror(fch);
		exit(EXIT_FAILURE);
	}

	while(fgets(l, MAXLINE, f)){
		ln++;

		if(*l == '#' || *l == '\n')	/* Ignore comments */
			continue;

		if((arg = striKWcmp(l,"Broker_Host="))){
			Broker_Host = removeLF(arg);
			if(debug)
				printf("\tBroker host : '%s'\n", Broker_Host);
		} else if((arg = striKWcmp(l,"Broker_Port="))){
#if defined(USE_PAHO)
			fprintf(stderr, "\nERROR line %u : When using Paho library, Broker_Port directive is not used.\n"
				"Instead, use\n"
				"\tBroker_Host=protocol://host:port\n", ln);
			exit(EXIT_FAILURE);
#else
			Broker_Port = atoi( arg );
			if(debug)
				printf("Broker port : %d\n", Broker_Port);
#endif
		} else if(*l == '*'){	/* New section */
			struct CSection *n = malloc( sizeof(struct CSection) );
			assert(n);

			assert( (n->name = strdup( removeLF(l+1) )) );	/* Name */

				/* Default value */
			n->port = NULL;
			n->standard = true;
			n->topic = n->cctopic = n->cptopic = NULL;

				/* Sections management */
			n->next = sections;
			sections = n;

			if(debug)
				printf("Entering section '%s'\n", n->name);
		} else if((arg = striKWcmp(l,"Port="))){	/* It's an historic section */
			if(!sections){
				fputs("*F* Configuration issue : Port directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			if(sections->port){
				fputs("*F* Configuration issue : Port directive used more than once in a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (sections->port = strdup( removeLF(arg) )) );
			sections->standard = false;

			if(debug)
				printf("\tHistoric frame\n\tSerial port : '%s'\n", sections->port);
		} else if((arg = striKWcmp(l,"Topic="))){
			if(!sections){
				fputs("*F* Configuration issue : Topic directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			if(sections->topic){
				fputs("*F* Configuration issue : Topic directive used more than once in a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (sections->topic = strdup( removeLF(arg) )) );
			if(debug)
				printf("\tTopic : '%s'\n", sections->topic);

		} else {
			fprintf(stderr, "\nERROR line %u : \"%s\" is not a known configuration directive\n", ln, removeLF(l));
			exit(EXIT_FAILURE);
		}
	}

	if(debug)
		puts("");

	fclose(f);
}

int main(int ac, char **av){
	const char *conf_file = DEFAULT_CONFIGURATION_FILE;
	
		/* reading arguments */
	int opt;
	while((opt = getopt(ac, av, "hdDf:")) != -1){
		switch(opt){
		case 'D':
			debug = 1;
		case 'd':
			debug += 1;
			printf("TeleInfod (%s) %s\n", VERSION, COPYRIGHT);
			break;
		case 'f':
			conf_file = optarg;
			break;
		case 'h':
			fprintf(stderr, "TeleInfod (%s) %s\n"
				"Publish TéléInfo figure to an MQTT broker\n"
				"Known options are :\n"
				"\t-h : this online help\n"
				"\t-d : enable debug messages\n"
				"\t-D : enable debug messages and display frame\n"
				"\t-f<file> : read <file> for configuration\n"
				"\t\t(default is '%s')\n",
				VERSION, COPYRIGHT, DEFAULT_CONFIGURATION_FILE
			);
			exit(EXIT_FAILURE);
		}
	}

	sections = NULL;
	read_configuration( conf_file );

	if(debug)
		puts("Starting ...");
}
