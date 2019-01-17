NAME = dwmstatus
VERSION = 1.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# flags
CPPFLAGS += -DVERSION=\"${VERSION}\"
CPPFLAGS += -I${X11INC}

CFLAGS ?= -O2 -pipe
CFLAGS += -std=c99 -pedantic -Wall

LDFLAGS = -L/usr/lib -lc -L${X11LIB} -lX11 -lasound
