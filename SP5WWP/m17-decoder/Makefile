m17-decoder-sym: m17-decoder-sym.c
	gcc -I ../../libm17 -Wall -Wextra -O2 m17-decoder-sym.c -o m17-decoder-sym -lm -lm17

install:
	sudo cp m17-decoder-sym /usr/local/bin

clean:
	rm -f m17-decoder-sym
