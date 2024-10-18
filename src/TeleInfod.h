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

#ifndef TELEINFO_H
#define TELEINFO_H

#include <stdio.h>

extern unsigned int debug;

extern char *removeLF(char *);
extern char *striKWcmp(char *, const char *);
extern const char *getLabel(FILE *, char *, char);
extern const char *getPayload(FILE *, char *, char, size_t);

extern int papub(const char *, int, void *, int);

extern void *process_historic(void *);
extern void *process_standard(void *);
#endif
