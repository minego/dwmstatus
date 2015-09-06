CC				= gcc
EXT				= .c
LDFLAGS			= -lX11 -lmpdclient
DEBUG_FLAGS		= -DDEBUG=1 -W -Wall -g
FLAGS			= -std=c99 -Wall -DMPD
FLAGS			+= ${DEBUG_FLAGS}
PREFIX			= /usr/local
MANPREFIX		= ${PREFIX}/share/man
PROG_NAME		= dwmstatus
SRCS			= dwmstatus.c

################################################################
#
################################################################

OBJS=${SRCS:${EXT}=.o}

${PROG_NAME}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS} ${FLAGS}

%.o: %${EXT}
	${CC} -o $@ -c $< ${FLAGS}

install: ${PROG_NAME}
	@echo installing executable file to ${PREFIX}/bin
	@mkdir -p ${PREFIX}/bin
	@cp -f ${PROG_NAME} ${PREFIX}/bin/${PROG_NAME}
	@chmod 755 ${PREFIX}/bin/${PROG_NAME}

all: ${PROG_NAME}

clean:
	rm -rf *.o ${PROG_NAME}

