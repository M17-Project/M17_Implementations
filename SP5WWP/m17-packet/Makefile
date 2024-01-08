all: m17-packet-encode m17-packet-decode

m17-packet-encode: m17-packet-encode.c
	gcc -I ../include -L ../lib -O2 -Wall -Wextra m17-packet-encode.c -o m17-packet-encode -lm -lm17

m17-packet-decode: m17-packet-decode.c
	gcc -I ../include -L ../lib -O2 -Wall -Wextra m17-packet-decode.c -o m17-packet-decode -lm -lm17

install: all
	sudo install m17-packet-encode /usr/local/bin
	sudo install m17-packet-decode /usr/local/bin

clean:
	rm -f m17-packet-encode m17-packet-decode
