all:
	gcc -g -Wall -o sfz sfz.c libfile.a

clean:
	rm -f *.o sfz
