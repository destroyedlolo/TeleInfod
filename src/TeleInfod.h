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

extern unsigned int debug;

extern char *removeLF(char *);
extern char *striKWcmp(char *, const char *);
extern char *extr_arg(char *, int);

extern int papub(const char *, int, void *, int);

#endif
