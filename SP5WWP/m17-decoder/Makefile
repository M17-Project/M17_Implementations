m17-decoder-sym: m17-decoder-sym.c golay.c golay.h viterbi.c viterbi.h crc.c crc.h ../inc/m17.h
	gcc -Wall -O2 m17-decoder-sym.c golay.c viterbi.c crc.c -o m17-decoder-sym -lm

clean:
	rm -f m17-decoder-sym
