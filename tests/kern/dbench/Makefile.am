AM_CFLAGS = -w

# VERSION=4.00

AM_CPPFLAGS = \
	-DDATADIR=\"$(X_DATADIR)\" \
	-I$(builddir)

LDADD = $(LIBPOPT)

check_PROGRAMS = dbench

dbench_SOURCES = \
	fileio.c \
	util.c \
	dbench.c \
	child.c \
	system.c \
	snprintf.c \
	dbench.h

check_SCRIPTS = \
	mkproto.pl

nodist_dbench_SOURCES = \
	proto.h

BUILT_SOURCES = proto.h

CLEANFILES = proto.h

proto.h: $(dbench_SOURCES)
	perl $(srcdir)/mkproto.pl $^ >$@

EXTRA_DIST = $(check_SCRIPTS)
