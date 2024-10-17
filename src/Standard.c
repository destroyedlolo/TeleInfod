/*
 *	Standard.c
 *		Handle Standard data
 *
 * Copyright 2015-2024 Laurent Faillie (destroyedlolo)
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

#include <stdlib.h>
#include <string.h>

#include "TeleInfod.h"
#include "Config.h"

void *process_standard(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *fframe;

			/* Target topics */

		/* Main */
	int sz = ctx->topic ? strlen(ctx->topic):0;	/* Size of its root */
	char topic[sz + 16]; /* topic + '/h' */
	if(sz){
		strcpy(topic, ctx->topic);
		topic[sz++] = '/';
	}

		/* Converted producer */
	int szcp = ctx->cptopic ? strlen(ctx->cptopic):0;
	char cptopic[szcp + 14];
	if(szcp){
		strcpy(cptopic, ctx->cptopic);
		cptopic[szcp++] = '/';
	}

		/* Converted Consumer */
	int szcc = ctx->cctopic ? strlen(ctx->cctopic):0;
	char cctopic[szcc + 14];
	if(szcc){
		strcpy(cctopic, ctx->cctopic);
		cctopic[szcc++] = '/';
	}

	if(debug)
		printf("Launching a processing historic for '%s'\n", ctx->name);

	char buffer[256];	/* the largest field content seems arount 120 ... but as there is no standard limit ... */
	
	if(!(fframe = fopen(ctx->port, "r" ))){
		perror(ctx->port);
		exit(EXIT_FAILURE);
	}

	while(getLabel(fframe, buffer, 0x09)){	/* Reading data */
		if(strstr(ctx->labels, buffer)){	/* Found in topic to publish */
			bool cpfound = false;	/* Found a topic to be converted for producer */
			bool ccfound = false;	/* Found a topic to be converted for consumer */
			bool horodate = (bool) strstr(
				"SMAXSN,SMAXSN1,SMAXSN2,SMAXSN3,"
				"SMAXSN-1,SMAXSN1-1,SMAXSN2-1,SMAXSN3-1,"
				"SMAXIN,SMAXIN-1,"
				"CCASN,CCASN-1,CCAIN,CCAIN-1"
				",UMOY1,UMOY2,UMOY3,"
				"DPM1,FPM1,DPM2,FPM2,DPM3,FPM3"
			, buffer);

			if(sz)	/* Full topic name */
				strcpy(topic + sz, buffer);
			if(szcp){
				cpfound = true;
				if(!strcmp(buffer,"SINSTI"))
					strcpy(cptopic + szcp, "PAPP");
				else if(!strcmp(buffer,"SINSTI"))
					strcpy(cptopic + szcp, "IINST");
				else if(!strcmp(buffer,"EAIT"))
					strcpy(cptopic + szcp, "BASE");
				else if(!strcmp(buffer,"SMAXIN"))
					strcpy(cptopic + szcp, "IMAX");
				else
					cpfound = false;
			}
			if(szcc){
				ccfound = true;
				if(!strcmp(buffer,"EAST"))
					strcpy(cctopic + szcc, "PAPP");
				else if(!strcmp(buffer,"IRMS1"))
					strcpy(cptopic + szcp, "IINST");
/*
Il faut sans doute jouer avec NGTF, LTARF et les index EASF01 et EASF02
A voir avec une vraie trame.

				else if(!strcmp(buffer,"????"))
					strcpy(cptopic + szcp, "HHPHC");
					strcpy(cptopic + szcp, "PTEC");
					strcpy(cptopic + szcp, "HCHC");
					strcpy(cptopic + szcp, "HCHP");
*/
				else
					ccfound = false;
			}

			if(!getPayload(fframe, buffer, 0x09, 256))
				break;	/* File is over */
			if(!*buffer)	/* Can't load the payload */
				continue;
	
			char *dt = buffer;
			if(horodate){	/* The date is embedded */
				dt = buffer + strlen(buffer) + 1;

				if(!getPayload(fframe, dt, 0x09, 256))
					break;	/* File is over */
				if(!*dt)	/* Can't load the payload */
					continue;
			}

			if(sz){
				if(debug){
					if(horodate)
						printf("*d* [%s] Publishing '%s' : '%s' '%s'\n", ctx->name, topic, buffer, dt);
					else
						printf("*d* [%s] Publishing '%s' : '%s'\n", ctx->name, topic, dt);
				}
				papub(topic, strlen(dt), dt, 0);
				if(horodate){
					strcat(topic, "/h");
					papub(topic, strlen(buffer), buffer, 0);
				}
			}

			if(cpfound){
				if(debug)
					printf("*d* [%s] Publishing '%s' : '%s'\n", ctx->name, cptopic, dt);
				papub(cptopic, strlen(dt), dt, 0);
			}
			if(ccfound){
				if(debug)
					printf("*d* [%s] Publishing '%s' : '%s'\n", ctx->name, cctopic, dt);
				papub(cctopic, strlen(dt), dt, 0);
			}
		}
	}

	if(debug){
		printf("*d*  [%s] Input stream closed : thread is finished.\n", ctx->name);
	}
	fclose(fframe);
	pthread_exit(0);
}
