m17-packet-encode: m17-packet-encode.c crc.c
	gcc -O2 -Wall m17-packet-encode.c crc.c -o m17-packet-encode -lm

m17-packet-decode: m17-packet-decode.c viterbi.c crc.c
	gcc -O2 -Wall m17-packet-decode.c viterbi.c crc.c -o m17-packet-decode -lm

all:
	gcc -O2 -Wall m17-packet-encode.c crc.c -o m17-packet-encode -lm
	gcc -O2 -Wall m17-packet-decode.c viterbi.c crc.c -o m17-packet-decode -lm

install:
	sudo install m17-packet-encode /usr/local/bin
	sudo install m17-packet-decode /usr/local/bin

clean:
	rm m17-packet-encode m17-packet-decode
