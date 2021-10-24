AM_CFLAGS = @GCCWARN@ \
	    -Wno-parentheses -Wno-error=parentheses

AM_CPPFLAGS = -I$(top_srcdir)/libdiod

noinst_LIBRARIES = liblsd.a

liblsd_a_SOURCES = \
	hostlist.c \
	hostlist.h \
	list.c \
	list.h \
	hash.c \
	hash.h \
	thread.c \
	thread.h \
	error.c
