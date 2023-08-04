#!/bin/bash
#
#	Rethinks makefiles as per dependancies.
#	This file is mostly used during development.

# Select which mqtt stack to use
# if set, use PAHO library, otherwise use Mosquitto's
USE_PAHO=1

# end of customisation area

# Error is fatal
set -e

if [ ${USE_PAHO+x} ]; then
	FLAGS='-DUSE_PAHO'
	LIBS='-lpaho-mqtt3c'
else	# Use Mosquitto one
	FLAGS=''
	LIBS='-lmosquitto'
fi

FLAGS="$FLAGS -Wall"
LIBS="-lpthread $LIBS"

cd src
LFMakeMaker -v +f=Makefile --opts="$FLAGS $LIBS" *.c -t=../TeleInfod > Makefile
cd ..
