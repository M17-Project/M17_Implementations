m17-packet-encode: m17-packet-encode.c crc.c
	gcc -O2 -Wall m17-packet-encode.c crc.c -o m17-packet-encode -lm

m17-packet-decode: m17-packet-decode.c viterbi.c crc.c
	gcc -O2 -Wall m17-packet-decode.c viterbi.c crc.c -o m17-packet-decode -lm

clean:
	rm m17-packet-encode m17-packet-decode
