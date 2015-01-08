termsynth:   termsynth.c
	gcc -Ofast -march=native termsynth.c -lasound -lpthread -lm -o termsynth

clean:
	rm termsynth
