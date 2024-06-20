m17-decoder-sym: m17-decoder-sym.c
	gcc -O2 -Wall -Wextra m17-decoder-sym.c ../../micro-ecc/uECC.c -o m17-decoder-sym -lm -lm17

install:
	sudo cp m17-decoder-sym /usr/local/bin

clean:
	rm -f m17-decoder-sym
