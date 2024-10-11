/*
 *	Historic.c
 *		Handle historic data
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

void *process_historic(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *fframe;

		/* Target topics */
	int sz = strlen(ctx->topic);	/* Size of its root */
	char topic[ sz + 14];
	strcpy(topic, ctx->topic);
	topic[sz++] = '/';

	if(debug)
		printf("Launching a processing historic for '%s'\n", ctx->name);

	char buffer[16];	/* 16 : for the largest field content */
	
	if(!(fframe = fopen( ctx->port, "r" ))){
		perror(ctx->port);
		exit(EXIT_FAILURE);
	}

	while(getLabel(fframe, buffer, 0x20)){	/* Reading data */
		if(strstr(ctx->labels, buffer)){	/* Found in topic to publish */
			strcpy(topic + sz, buffer);

			if(!getPayload(fframe, buffer, 0x20, 16))
				break;	/* File is over */
	
			if(!*buffer)	/* Can't load the payload */
				continue;
	
			if(debug)
				printf("*d* [%s] Publishing '%s' : '%s'\n", ctx->name, topic, buffer);
		}
	}
	fclose(fframe);
	pthread_exit(0);
}
