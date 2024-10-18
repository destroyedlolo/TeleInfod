# makefile created automaticaly by LFMakeMaker
# LFMakeMaker 1.6 (Feb  8 2024 13:06:32) (c)LFSoft 1997

gotoall: all


#The compiler (may be customized for compiler's options).
cc=cc
opts=-DUSE_PAHO -Wall -lpthread -lpaho-mqtt3c

Historique.o : Historique.c TeleInfod.h Config.h Makefile 
	$(cc) -c -o Historique.o Historique.c $(opts) 

Standard.o : Standard.c TeleInfod.h Config.h Makefile 
	$(cc) -c -o Standard.o Standard.c $(opts) 

TeleInfod.o : TeleInfod.c Version.h Config.h TeleInfod.h Makefile 
	$(cc) -c -o TeleInfod.o TeleInfod.c $(opts) 

../TeleInfod : TeleInfod.o Standard.o Historique.o Makefile 
	 $(cc) -o ../TeleInfod TeleInfod.o Standard.o Historique.o \
  $(opts) 

all: ../TeleInfod 
