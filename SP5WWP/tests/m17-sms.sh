#!/bin/bash

#check input parameters
if [ "$#" -lt 5 ]
then
	echo "Not enough parameters."
	echo "Usage: sh m17-sms.sh dst src can message output_file"
	echo "Exiting."
	exit
fi

#encode the message (WAVE file output)
m17-packet-encode -w -S $2 -D $1 -C $3 -T "$4" -o $5
