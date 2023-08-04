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

#include "Version.h"

unsigned int debug = 0;

int main(int ac, char **av){
	
		/* reading arguments */
	int opt;
	while((opt = getopt(ac, av, "hdDf:")) != -1){
		switch(opt){
		case 'D':
			debug = 1;
		case 'd':
			debug += 1;
			printf("TeleInfod (%s) %s\n", VERSION, COPYRIGHT);
			puts("Starting ...");
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
}
