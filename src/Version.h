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
 *
 * Note : I didn't find any information if the TéléInfo frame is French Only or if it's an
 * 	International norm. So, by default, I put comment in English.
 *
 *		03/05/2015 - v0.1 LF - Start of development, using Mosquitto's own library, synchronous
 *		04/05/2015 - v0.2 LF - Add Paho library and asynchronous calls
 *		05/05/2015 - v0.3 LF - main infinite loop implemented
 *							- add retained option when using Paho
 *							- live data moved to .../values/
 *		05/05/2015 - v1.0 LF - Add summary
 *							- release as v1.0
 *		06/05/2015 - v1.1 LF - Send only strings to avoid endianness issue
 *		29/07/2015 - v1.2 LF - Allow 0 sample delay : in this case, ttyS stay open
 *					-------
 *		25/08/2015 - v2.0 LF - Add monitoring period
 *		22/10/2015 - v2.1 LF - Improve Mosquitto handling
 *				- Add fields PTEC, IMAX, ISOUSC, HHPHC, OPTARIF
 *		21/03/2017 - v2.2 LF - in verbose mode, display when new frame arrives
 *		25/07/2017 - v2.3 LF - Add a message when we are waiting for frames
 *		10/09/2017 - v2.4 LF - Add -dd for a better debugging
 *				- Add HHPHC in summary
 *		10/09/2017 - v2.5 LF - Add PTEC in summary
 *					-------
 *		04/06/2021 - v3.0 LF - handly Linky's standard frame
 *							- remove c99 dependancies
 *		29/07/2023 - v3.1 LF - Publish date as well for standard frame
 *					-------
 *		05/07/2023 - V4.0 LF - redesign code
 */

#ifndef VERSION

#define VERSION "V4.00.05"
#define COPYRIGHT "(c) L.Faillie 2015-24"

#endif
