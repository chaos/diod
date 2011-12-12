AM_CFLAGS = @GCCWARN@

# VERSION=4.00

AM_CPPFLAGS = -DDATADIR=\"$(X_DATADIR)\"

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

nodist_dbench_SOURCES = \
	proto.h

BUILT_SOURCES = proto.h

CLEANFILES = proto.h

proto.h: $(dbench_SOURCES) mkproto.pl
	./mkproto.pl $(dbench_SOURCES) > proto.h

EXTRA_DIST = mkproto.pl
