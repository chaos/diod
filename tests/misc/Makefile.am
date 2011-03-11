check_PROGRAMS = tfcntl tsetfsuid tsetfsuidsupp tsetuid tsuppgrp topt

TESTS = t00 t01 t02 t03 t04 t05

CLEANFILES = *.out *.diff *.stats

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod \
	-I$(top_srcdir)/diodmount


LDADD = $(top_builddir)/libdiod/libdiod.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = \
	$(top_srcdir)/diodmount/opt.c \
	$(top_srcdir)/diodmount/opt.h \
	test.h

tsetuid_SOURCES = tsetuid.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tsetfsuid_SOURCES = tsetfsuid.c $(common_sources)
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c $(common_sources)
tfcntl_SOURCES = tfcntl.c $(common_sources)
topt_SOURCES = topt.c $(common_sources) 
