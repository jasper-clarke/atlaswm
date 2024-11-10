# AtlasWM - navigating the world of the windows

include config.mk

SRC = draw.c atlas.c util.c layouts.c configurer.c ipc.c windows.c events.c dashboard.c ewmh.c
OBJ = ${SRC:.c=.o}

all: atlaswm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	cp config.def.h $@

atlaswm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f atlaswm ${OBJ} atlaswm-${VERSION}.tar.gz

dist: clean
	mkdir -p atlaswm-${VERSION}
	cp -R LICENSE Makefile README config.def.h config.mk\
		atlaswm.1 util.h ${SRC} atlaswm.png transient.c atlaswm-${VERSION}
	tar -cf atlaswm-${VERSION}.tar atlaswm-${VERSION}
	gzip atlaswm-${VERSION}.tar
	rm -rf atlaswm-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f atlaswm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/atlaswm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < atlaswm.1 > ${DESTDIR}${MANPREFIX}/man1/atlaswm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/atlaswm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/atlaswm\
		${DESTDIR}${MANPREFIX}/man1/atlaswm.1

.PHONY: all clean dist install uninstall
