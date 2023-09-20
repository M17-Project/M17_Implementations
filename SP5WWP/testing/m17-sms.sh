#!/bin/bash

if [ "$#" -lt 5 ]
then
	echo "Not enough parameters."
	echo "Usage: sh m17-sms.sh dst src can message output_file"
	echo "Exiting."
	exit
fi

str=$4
printf -v _ %s%n "$str" len #get UTF-8 encoded length
len=$(($len+2)) #add the 2 additional bytes (0x05 and 0x00)
echo -en "\x05"$str"\x00" | m17-packet-encode -S $2 -D $1 -C $3 -n $len -o $5
sox -t raw -r 48000 -b 16 -c 1 -L -e signed-integer $5 $5".wav"
