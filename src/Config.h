/*
 * TeleInfod
 * 	A daemon to publish EDF's "Télé Information" to a MQTT broker
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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct CSection {	/* Section of the configuration : a TéléInfo flow */
	struct CSection *next;	/* Next section */
	const char *name;		/* help to have understandable error messages */
	pthread_t thread;
	const char *port;		/* Where to read */
	const char *labels;		/* Label to publish */
	bool standard;			/* true : standard frames, false : historic */
	const char *topic;		/* main topic */
	const char *cctopic;	/* Converted Customer topic */
	const char *cptopic;	/* Converted Producer topic */
};

	/* Where to find default configuration file */
#define DEFAULT_CONFIGURATION_FILE "/usr/local/etc/TeleInfod.conf"

	/* Keep alive signal to the broker */
#define BRK_KEEPALIVE 60

	/* Maximum length of a line to be read */
#define MAXLINE 1024

	/* Shared objects */
extern unsigned int debug;

#endif
