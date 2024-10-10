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

#include "TeleInfod.h"
#include "Config.h"

void *process_historic(void *actx){
	struct CSection *ctx = actx;	/* Only to avoid zillions of cast */
	FILE *fframe;

	if(debug)
		printf("Launching a processing historic for '%s'\n", ctx->name);

	char buffer[16];	/* 16 : for the largest field content */
	
	if(!(fframe = fopen( ctx->port, "r" ))){
		perror(ctx->port);
		exit(EXIT_FAILURE);
	}

	while(getLabel(fframe, buffer, 0x20)){	/* Reading data */
		printf("--> '%s'\n", buffer);
	}
	fclose(fframe);
	pthread_exit(0);
}
