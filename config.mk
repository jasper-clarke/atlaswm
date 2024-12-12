# AtlasWM version
VERSION = 0.1.0

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# freetype
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2

# includes and libs
INCS = -I${X11INC} -I${FREETYPEINC}
LIBS = -L${X11LIB} -lX11 -lXinerama ${FREETYPELIBS}

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L -DVERSION=\"${VERSION}\" -DXINERAMA -DCMAKE_EXPORT_COMPILE_COMMANDS=1
CFLAGS   = -std=gnu17 -pedantic -Wall -Wno-deprecated-declarations -Wno-format-truncation -Wno-variadic-macros -Wno-gnu-zero-variadic-macro-arguments -O2 ${INCS} ${CPPFLAGS}
LDFLAGS  = ${LIBS}

# compiler and linker
CC = gcc
