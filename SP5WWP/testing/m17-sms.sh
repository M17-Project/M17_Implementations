#!/bin/bash

#check input parameters
if [ "$#" -lt 5 ]
then
	echo "Not enough parameters."
	echo "Usage: sh m17-sms.sh dst src can message output_file"
	echo "Exiting."
	exit
fi

#extract string input
str=$4

#get the size in bytes
len=$(echo -n "$str" | wc -c)  #get UTF-8 encoded length in bytes
len=$((len+2))  #add 2 additional bytes (0x05 and 0x00)
len_chars=${#str} #get length in characters rather than bytes

#echo the message to the encoder
str1=${str:0:$((len_chars-1))}
str2=${str: -1}
echo -en "\x05"$str1\'$str2\'"\x00" | m17-packet-encode -S $2 -D $1 -C $3 -n $len -o $5
sox -t raw -r 48000 -b 16 -c 1 -L -e signed-integer $5 $5".wav"

