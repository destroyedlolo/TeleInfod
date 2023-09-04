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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct CSection {	/* Section of the configuration : a TéléInfo flow */
	struct CSection *next;	/* Next section */
	const char *name;		/* help to have understandable error messages */
	pthread_t thread;
	const char *port;		/* Where to read */
	bool standard;			/* true : standard frames, false : historic */
	const char *topic;		/* main topic */
	const char *cctopic;	/* Converted Customer topic */
	const char *cptopic;	/* Converted Producer topic */
};

#endif
