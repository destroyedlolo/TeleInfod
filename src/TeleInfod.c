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
#include "TeleInfod.h"

unsigned int debug = 0;
static const char *Broker_Host;
#ifdef USE_MOSQUITTO
static int Broker_Port;
#endif
static struct CSection *sections;

#ifdef USE_MOSQUITTO
static struct mosquitto *mosq;
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
 * Returns :
 * 	- remaining string if the keyword matches
 * 	- NULL if the keyword is not found
 */
	size_t klen = strlen(kw);
	if( strncasecmp(s,kw,klen) )
		return NULL;
	else
		return s+klen;
}


	/* **
	 * Trames handling
	 * **/
const char *getLabel(FILE *f, char *buffer){
/* Wait for the next label and store it in the buffer
 * -> buffer : char [9]
 * <- the buffer filled with the label
 *	NULL if the file is over
 */
 	int c;
	while(1){
		do {
			c=fgetc(f);
			if(c == EOF)
				return NULL;
		} while(c != 0x0a);

		for(i=0; i==9 || c==0x09; i++){
		}
	}
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
			assert( (Broker_Host = strdup(removeLF(arg))) );
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
			n->labels = NULL;
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
		} else if((arg = striKWcmp(l,"SPort="))){	/* It's a standard section */
			if(!sections){
				fputs("*F* Configuration issue : SPort directive outside a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			if(sections->port){
				fputs("*F* Configuration issue : SPort directive used more than once in a section\n", stderr);
				exit(EXIT_FAILURE);
			}
			assert( (sections->port = strdup( removeLF(arg) )) );
			sections->standard = true;

			if(debug)
				printf("\tStandard frame\n\tSerial port : '%s'\n", sections->port);
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
		} else if((arg = striKWcmp(l,"Publish="))){
			assert( (sections->labels = strdup( removeLF(arg) )) );
			if(debug)
				printf("\tLabels : '%s'\n", sections->labels);
		} else {
			fprintf(stderr, "\nERROR line %u : \"%s\" is not a known configuration directive\n", ln, removeLF(l));
			exit(EXIT_FAILURE);
		}
	}

	if(debug)
		puts("");

	fclose(f);
}

#ifdef USE_MOSQUITTO
int papub( const char *topic, int length, void *payload, int retained ){	/* Custom wrapper to publish */
	switch(mosquitto_publish(mosq, NULL, topic, length, payload, 0, retained ? true : false)){
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
static int msgarrived(void *ctx, char *topic, int tlen, MQTTClient_message *msg){
	if(debug)
		printf("*I* Unexpected message arrival (topic : '%s')\n", topic);

	MQTTClient_freeMessage(&msg);
	MQTTClient_free(topic);
	return 1;
}

static void connlost(void *ctx, char *cause){
	printf("*W* Broker connection lost due to %s\n", cause);
}

int papub( const char *topic, int length, void *payload, int retained ){	/* Custom wrapper to publish */
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.retained = retained;
	pubmsg.payloadlen = length;
	pubmsg.payload = payload;

	return MQTTClient_publishMessage( client, topic, &pubmsg, NULL);
}
#endif

static void theend(void){
		/* Some cleanup */
#ifdef USE_MOSQUITTO
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
#elif defined(USE_PAHO)
	MQTTClient_disconnect(client, 10000);	/* 10s for the grace period */
	MQTTClient_destroy(&client);
#endif
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

	if(!sections){
		fputs("*F* No section defined : giving up ...\n", stderr);
		exit(EXIT_FAILURE);
	}

	if(debug){
		printf("Sanity checks : ");
		fflush( stdout );
	}

	for(struct CSection *s = sections ; s; s = s->next ){
		if(!s->port){
			fprintf( stderr, "*F* No port defined for section '%s'\n", s->name );
			exit(EXIT_FAILURE);
		}

		if(!s->labels){
			fprintf( stderr, "*F* Publishing missing for section '%s'\n", s->name );
			exit(EXIT_FAILURE);
		}

		if(s->standard){	/* check specifics for standard frames */
			if(!s->topic && !s->cctopic && !s->cptopic){
				fprintf( stderr, "*F* at least Topic, ConvCons or ConvProd has to be provided for standard section '%s'\n", s->name );
				exit(EXIT_FAILURE);
			}
			if(s->cctopic){
				fprintf( stderr, "*F* ConvCons is not yet implemented as per v3.0 in standard section '%s'\n", s->name );
				exit(EXIT_FAILURE);
			}
		} else {	/* check specifics for historic frames */
			if(!s->topic){
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
	if(!(mosq = mosquitto_new(
		"TeleInfod",	/* Id for this client */
		true,			/* clean msg on exit */
		NULL			/* No call backs */
	))){
		perror("Moquitto_new()");
		mosquitto_lib_cleanup();
		exit(EXIT_FAILURE);
	}

	switch( mosquitto_connect(mosq, Broker_Host, Broker_Port, BRK_KEEPALIVE) ){
	case MOSQ_ERR_INVAL:
		fputs("Invalid parameter for mosquitto_connect()\n", stderr);
		mosquitto_destroy(mosq);
		mosquitto_lib_cleanup();
		exit(EXIT_FAILURE);
	case MOSQ_ERR_ERRNO:
		perror("mosquitto_connect()");
		mosquitto_destroy(mosq);
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

printf("---> '%s'\n", Broker_Host);
		int err;
		if((err = MQTTClient_create( &client, Broker_Host, "TeleInfod", MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS){
			fprintf(stderr, "Failed to create client : %d\n", err);
			exit(EXIT_FAILURE);
		}
		MQTTClient_setCallbacks( client, NULL, connlost, msgarrived, NULL);

		switch( (err = MQTTClient_connect( client, &conn_opts)) ){
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
		case MQTTCLIENT_BAD_STRUCTURE:
			fputs("Header / Library mismatch : recompilation is needed !", stderr);
			exit(EXIT_FAILURE);
		default :
			fprintf(stderr, "Unable to connect (%d)\n", err);
			exit(EXIT_FAILURE);
		}
	}
#endif

	atexit(theend);

	if(debug)
		puts("Starting ...");
}
