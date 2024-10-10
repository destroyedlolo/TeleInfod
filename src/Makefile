# makefile created automaticaly by LFMakeMaker
# LFMakeMaker 1.6 (May  7 2022 20:46:23) (c)LFSoft 1997

gotoall: all


#The compiler (may be customized for compiler's options).
cc=cc
opts=-DUSE_PAHO -Wall -lpthread -lpaho-mqtt3c

Historique.o : Historique.c TeleInfod.h Config.h Makefile 
	$(cc) -c -o Historique.o Historique.c $(opts) 

TeleInfod.o : TeleInfod.c Version.h Config.h TeleInfod.h Makefile 
	$(cc) -c -o TeleInfod.o TeleInfod.c $(opts) 

../TeleInfod : TeleInfod.o Historique.o Makefile 
	 $(cc) -o ../TeleInfod TeleInfod.o Historique.o $(opts) 

all: ../TeleInfod 
