check_PROGRAMS = tfcntl tsetfsuid tsetfsuidsupp tsetuid tsuppgrp

TESTS = t00 t01 t02 t03 t04 t05

CLEANFILES = *.out *.diff *.stats

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I../libnpfs -I../liblsd -I../libdiod


LDADD = $(top_builddir)/libdiod/libdiod.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = \
	test.h

tsetuid_SOURCES = tsetuid.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tsetfsuid_SOURCES = tsetfsuid.c $(common_sources)
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c $(common_sources)
tfcntl_SOURCES = tfcntl.c $(common_sources)
