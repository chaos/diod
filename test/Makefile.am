check_PROGRAMS = tsetuid tsetfsuid tsetfsuidsupp tsuppgrp

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/src/diod"
TESTS = t00 t01 t02

CLEANFILES = *.out *.diff

AM_CPPFLAGS = \
        -I../npfs/include -I../liblsd -I../common


LDADD = $(top_builddir)/common/libdiod.a \
        $(top_builddir)/npfs/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = \
	test.h

tsetuid_SOURCES = tsetuid.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tsetfsuid_SOURCES = tsetfsuid.c $(common_sources)
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c $(common_sources)
