/*
 * SimuleTrames
 * 	Quick and dirty TeleInfod companion that sending double 
 * 	"TeleInformation" frame, one with HC/HP (consomation), another
 * 	with BASE (production), to a fifo pipes.
 *
 *	SimuleTrames is used to test TeleInfod.
 *
 * Compilation :
gcc -Wall SimuleTrames.c -o SimuleTrame
 *
 * Copyright 2015 Laurent Faillie
 *
 * 		TeleInfod is covered by 
 *      Creative Commons Attribution-NonCommercial 3.0 License
 *      (http://creativecommons.org/licenses/by-nc/3.0/) 
 *      Consequently, you're free to use if for personal or non-profit usage,
 *      professional or commercial usage REQUIRES a commercial licence.
 *  
 *      TeleInfod is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	23/08/2015 - v1	LF - First version
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#define FPROD "/tmp/prod"
#define FCONSO "/tmp/conso"

	/* Notez-bien : QUICK & DIRTY ! */

FILE *fdp=NULL, *fdc=NULL;
unsigned long int cntp, cntc;

void theend( void ){
	unlink( FPROD );
	unlink( FCONSO );
}

void handleInt(int na){
	exit(EXIT_SUCCESS);
}

int main(){
	cntp = (unsigned long int)clock();
	cntc = (unsigned long int)time(NULL);

		/* Create fifo */
	assert( !mkfifo(FPROD, 0666) );
	atexit( theend );
	assert( !mkfifo(FCONSO, 0666) );

	assert( fdp = fopen(FPROD, "w") );
/*
	assert( fdc = fopen(FCONSO, "w") );
*/
	signal(SIGINT, handleInt);

	for(;;){
		cntp += ((unsigned long int)time(NULL) % clock())/100;

		fprintf(fdp, "HCHC %09ld +\r\n", cntp);

		fflush( fdp );
		sleep(1);
	}
	return 0;
}
