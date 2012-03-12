#
#	Makefile for experimental Actor Based Environment
#

#CFLAGS=	-ansi -O3 -DNDEBUG -DDBUG_OFF
#CFLAGS=	-ansi -O -DNDEBUG -DDBUG_OFF
#CFLAGS=	-ansi -g
#CFLAGS=	-ansi -pedantic
CFLAGS=	-ansi -pedantic -Wall

LIB=	libabe.a
LHDRS=	actor.h emit.h atom.h gc.h cons.h sbuf.h dbug.h types.h
LOBJS=	actor.o emit.o atom.o gc.o cons.o sbuf.o dbug.o

LIBS=	$(LIB) -lm

PROGS=	abe challenge echallenge life reduce schemer kernel
JUNK=	*.exe *.stackdump *.dbg core *~

all: $(LIB) $(PROGS)

clean:
	rm -f $(LIB) $(PROGS) $(JUNK) *.o

test: $(PROGS)
	rm -f *.dbg
	./abe -t -#d:t:o,abe.dbg
#	./reduce -t -#d:t:o,reduce.dbg
#	./schemer -t -#d:t:o,schemer.dbg
	./kernel -t -#d:t:o,kernel.dbg

$(LIB): $(LOBJS)
	$(AR) rv $(LIB) $(LOBJS)

$(LOBJS): $(LHDRS)

abe: abe.o sample.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ abe.o sample.o $(LIBS)

challenge: challenge.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ challenge.o $(LIBS)

echallenge: echallenge.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ echallenge.o $(LIBS)

life: life.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ life.o $(LIBS)

reduce: reduce.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ reduce.o $(LIBS)

schemer: schemer.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ schemer.o $(LIBS)

kernel: kernel.o $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ kernel.o $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $<

