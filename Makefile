
SDLCFLAGS=$(shell sdl2-config --cflags)
SDLLIBS=$(shell sdl2-config --libs)

CFLAGS=-fsanitize=address -Wall -Wextra --pedantic ${SDLCFLAGS} -g3
LDFLAGS=-fsanitize=address

all:	biro-hero

biro-hero.o:	biro-hero.c
	${CC} ${CFLAGS} -c -o biro-hero.o biro-hero.c

biro-hero:	biro-hero.o
	@echo SDLLIBS=${SDLLIBS}
	@echo CFLAGS=${CFLAGS}
	${CC} ${CFLAGS} -o biro-hero ${LDFLAGS} biro-hero.o ${SDLLIBS}

clean:
	rm -f biro-hero *.o
