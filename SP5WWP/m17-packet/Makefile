all: m17-packet-encode m17-packet-decode

m17-packet-encode: m17-packet-encode.c ../lib/m17lib.c ../lib/m17lib.h
	gcc -O2 -Wall m17-packet-encode.c ../lib/m17lib.c -o m17-packet-encode -lm

m17-packet-decode: m17-packet-decode.c ../lib/m17lib.c ../lib/m17lib.h
	gcc -O2 -Wall m17-packet-decode.c ../lib/m17lib.c -o m17-packet-decode -lm

install: all
	sudo install m17-packet-encode /usr/local/bin
	sudo install m17-packet-decode /usr/local/bin

clean:
	rm -f m17-packet-encode m17-packet-decode
