/*
 * SimuleTrames
 * 	Quick and dirty TeleInfod companion that sending double 
 * 	"TeleInformation" frame, one with HC/HP (consomation), another
 * 	with BASE (production), to a fifo pipes.
 *
 *	SimuleTrames is used to test TeleInfod.
 *
 * Compilation :
gcc -Wall SimuleTrames.c -o SimuleTrames
 * or
gcc -DSTRESS=10000 -Wall SimuleTrames.c -o SimuleTrames
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
 *	23/08/2015 - v1		LF - First version
 *	22/10/2015 - v1.1 	LF - Add some fields + conditionnaly compile production frame
 *	16/07/2016 - v1.2	LF - w/ STRESS set, usleep replace sleep to flood the network
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
#ifdef STRESS
#include <unistd.h>
#endif

#define FCONSO "/tmp/conso"
#define FPROD "/tmp/prod"

	/* Notez-bien : QUICK & DIRTY ! */

FILE *fdp=NULL, *fdc=NULL;

void theend( void ){
	fclose( fdc );
	unlink( FCONSO );

#ifdef FPROD
	fclose( fdp );
	unlink( FPROD );
#endif
}

void handleInt(int na){
	exit(EXIT_SUCCESS);
}

int main(){
	unsigned long int cntcp = (unsigned long int)clock(),
		cntcc = ((unsigned long int)time(NULL) % clock()),
		cntp = (unsigned long int)time(NULL);

		/* Create fifo */
	mkfifo(FCONSO, 0666);
#ifdef FPROD
	mkfifo(FPROD, 0666);
#endif
	assert( fdc = fopen(FCONSO, "w") );
	atexit( theend );

#ifdef FPROD
	assert( fdp = fopen(FPROD, "w") );
#else
	puts("*W* Prod not enabled");
#endif

	signal(SIGINT, handleInt);

	for(;;){
		unsigned long int pap = ((unsigned long int)time(NULL) % clock())/10;
		if(pap % 2)
			cntcp += pap;
		else
			cntcc += pap;

		unsigned long int pac = ((unsigned long int)time(NULL) % clock())/10;
		cntp += pac;

		fprintf(fdc, "ADCO 012345678901 B\r\nOPTARIF HC.. <\r\nISOUSC 60 <\r\nPTEC HP..  \r\n");
		fprintf(fdc, "IMAX 062 G\r\nINST %03ld Y\r\nPAPP %05ld +\r\n", pap / 220, pap);
		fprintf(fdc, "HCHC %09ld Y\r\nHCHP %09ld +\r\nHHPHC %c .\r\n", cntcc, cntcp, (pap % 2)?'P':'C');
		fprintf(fdc, "MOTDETAT 000000 B\r%c\b\n",3);

		fflush( fdc );

#ifdef FPROD
		fprintf(fdp, "TDETAT 000000 B\r\nADCO 987165432101 B\r\nOPTARIF BASE 0\r\nISOUSC 15 <\r\n");
		fprintf(fdp, "BASE %09ld ,\r\nIINST %03ld Y\r\nPAPP %05ld +\r\n", cntp, pac / 220, pac);
		fflush( fdp );
#endif

#ifdef STRESS
		usleep(STRESS);
#else
		sleep(1);
#endif
	}
	return 0;
}
