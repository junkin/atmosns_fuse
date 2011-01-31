LIBS=-latmos -L/usr/local/lib/ -I/usr/local/include -lssl -lcurl -lmemcached
FLAGS = -D_FILE_OFFSET_BITS=64
atmosns_fuse : atmosns_fuse.o aollog.o
	gcc -g `pkg-config fuse --libs` -o atmosns_fuse atmosns_fuse.o aollog.o ${INC} ${LIBS}

atmosns_fuse.o : atmosns_fuse.c aollog.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` ${LIBS} ${INC} -c atmosns_fuse.c

aollog.o : aollog.c aollog.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` ${LIBS} ${INC} -c aollog.c

clean:
	rm -f atmosns_fuse *.o
