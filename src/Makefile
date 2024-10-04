# makefile created automaticaly by LFMakeMaker
# LFMakeMaker 1.6 (Nov 18 2023 14:49:11) (c)LFSoft 1997

gotoall: all


#The compiler (may be customized for compiler's options).
cc=cc
opts=-DUSE_PAHO -Wall -lpthread -lpaho-mqtt3c

TeleInfod.o : TeleInfod.c Version.h Config.h TeleInfod.h Makefile 
	$(cc) -c -o TeleInfod.o TeleInfod.c $(opts) 

../TeleInfod : TeleInfod.o Makefile 
	 $(cc) -o ../TeleInfod TeleInfod.o $(opts) 

all: ../TeleInfod 
