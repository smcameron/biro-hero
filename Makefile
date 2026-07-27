
SDLCFLAGS=$(shell sdl2-config --cflags)
SDLLIBS=$(shell sdl2-config --libs)

PKG_CONFIG=pkg-config

SNDLIBS:=$(shell $(PKG_CONFIG) --libs portaudio-2.0 vorbisfile)
SNDFLAGS:=$(shell $(PKG_CONFIG) --cflags portaudio-2.0)

SNDLIBS:=$(shell $(PKG_CONFIG) --libs portaudio-2.0 vorbisfile)

LIBS=${SDLLIBS} -lpng -lm ${SNDLIBS}

CFLAGS=-fsanitize=address -Wall -Wextra --pedantic ${SDLCFLAGS} -g3 -pthread ${SNDFLAGS}
LDFLAGS=-fsanitize=address

OBJFILES=png_utils.o snis_alloc.o stacktrace.o vec3.o wwviaudio.o ogg_to_pcm.o

all:	biro-hero

png_utils.o:	png_utils.c png_utils.h
	${CC} ${CFLAGS} -c -o png_utils.o png_utils.c

snis_alloc.o:	snis_alloc.h snis_alloc.c stacktrace.h
	${CC} ${CFLAGS} -c -o snis_alloc.o snis_alloc.c

wwviaudio.o:	wwviaudio.h wwviaudio.c
	${CC} ${CFLAGS} -c -o wwviaudio.o wwviaudio.c

ogg_to_pcm.o:	ogg_to_pcm.h ogg_to_pcm.c
	${CC} ${CFLAGS} -c -o ogg_to_pcm.o ogg_to_pcm.c

vec3.o:	vec3.h vec3.c
	${CC} ${CFLAGS} -c -o vec3.o vec3.c

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

