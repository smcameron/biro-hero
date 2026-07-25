
SDLCFLAGS=$(shell sdl2-config --cflags)
SDLLIBS=$(shell sdl2-config --libs)

LIBS=${SDLLIBS} -lpng -lm

CFLAGS=-fsanitize=address -Wall -Wextra --pedantic ${SDLCFLAGS} -g3
LDFLAGS=-fsanitize=address

OBJFILES=png_utils.o snis_alloc.o stacktrace.o

all:	biro-hero

png_utils.o:	png_utils.c png_utils.h
	${CC} ${CFLAGS} -c -o png_utils.o png_utils.c

snis_alloc.o:	snis_alloc.h snis_alloc.c stacktrace.h
	${CC} ${CFLAGS} -c -o snis_alloc.o snis_alloc.c

stacktrace.o:	stacktrace.h stacktrace.c
	${CC} ${CFLAGS} -c -o stacktrace.o stacktrace.c

biro-hero.o:	biro-hero.c png_utils.h
	${CC} ${CFLAGS} -c -o biro-hero.o  biro-hero.c

biro-hero:	biro-hero.o ${OBJFILES}
	@echo SDLLIBS=${SDLLIBS}
	@echo CFLAGS=${CFLAGS}
	${CC} ${CFLAGS} -o biro-hero ${LDFLAGS} ${LIBS} biro-hero.o ${SDLLIBS} ${OBJFILES}

clean:
	rm -f biro-hero *.o

