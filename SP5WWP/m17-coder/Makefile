m17-coder-sym: m17-coder-sym.c
	gcc -I ../../libm17 -L ../../libm17 -O2 -Wall -Wextra m17-coder-sym.c -o m17-coder-sym -lm -lm17

install:
	sudo cp m17-coder-sym /usr/local/bin

clean:
	rm -f m17-coder-sym
