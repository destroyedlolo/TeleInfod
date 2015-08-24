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

void theend( void ){
	fclose( fdc );

	unlink( FPROD );
	unlink( FCONSO );
}

void handleInt(int na){
	exit(EXIT_SUCCESS);
}

int main(){
	unsigned long int cntcp = (unsigned long int)clock(),
		cntcc = ((unsigned long int)time(NULL) % clock()),
		cntp = (unsigned long int)time(NULL);

		/* Create fifo */
	assert( !mkfifo(FPROD, 0666) );
	atexit( theend );
	assert( !mkfifo(FCONSO, 0666) );

/*
	assert( fdp = fopen(FPROD, "w") );
*/
	assert( fdc = fopen(FCONSO, "w") );

	signal(SIGINT, handleInt);

	for(;;){
		unsigned long int pap = ((unsigned long int)time(NULL) % clock())/10;
		if(pap % 2)
			cntcp += pap;
		else
			cntcc += pap;

		fprintf(fdc, "ADCO 012345678901 B\r\nOPTARIF HC.. <\r\nISOUSC 60 <\r\n");
		fprintf(fdc, "IINST %03ld Y\r\nPAPP %05ld +\r\n", pap / 220, pap);
		fprintf(fdc, "HCHC %09ld Y\r\nHCHP %09ld +\r\n", cntcc, cntcp);
		fprintf(fdc, "MOTDETAT 000000 B\r%c\b\n",3);

		fflush( fdc );
		sleep(1);
	}
	return 0;
}
