
CFLAGS=-g -std=c99 -I../
LDFLAGS=-L. -les2ts -lavformat -lavcodec -lpthread

all:	libes2ts.a libes2ts.so stream convert
#	./convert stream01.nals

clean:
	rm -f convert *.o output.mpegts libes2ts.a stream libes2ts.so

convert:	convert.c
	gcc -g -std=c99 convert.c -lavformat -lavcodec -o convert

es2ts.o:	es2ts.c es2ts.h xorg-list.h
	gcc $(CFLAGS) es2ts.c -c

libes2ts.a:	es2ts.o
	ar -r libes2ts.a es2ts.o

libes2ts.so:	libes2ts.a
	gcc -shared es2ts.o -o libes2ts.so

stream:	libes2ts.a stream.c
	gcc $(CFLAGS) stream.c $(LDFLAGS) -o stream

tarball:
	cd .. && tar zcf libes2ts-$(shell date +%Y%m%d-%H%M%S).tgz --exclude-vcs ./libes2ts

install:
	mkdir -p /usr/include/libes2ts
	cp -f es2ts.h /usr/include/libes2ts
	cp -f xorg-list.h /usr/include/libes2ts
	cp -f libes2ts.a /usr/lib
	cp -f libes2ts.so /usr/lib

