#!/bin/bash

#CONT=0
for CONT in $(seq 15)
do
	echo remove $CONT > /proc/modlist
	echo -e $CONT :
	cat /proc/modlist
	echo "**************"
	sleep .4
done
