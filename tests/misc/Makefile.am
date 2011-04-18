check_PROGRAMS = \
	tfcntl \
	tsetfsuid \
	tsetfsuidsupp \
	tsetuid \
	tsuppgrp \
	topt \
	tconf \
	tserialize \
	tlist \
	tnpsrv

TESTS = t00 t01 t02 t03 t04 t05 t06 t07 t08 t09 t10
#XFAIL_TESTS = t10

CLEANFILES = *.out *.diff *.stats

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod \
	-I$(top_srcdir)/utils


LDADD = $(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = \
	$(top_srcdir)/utils/opt.c \
	$(top_srcdir)/utils/opt.h \
	test.h

tsetuid_SOURCES = tsetuid.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tsetfsuid_SOURCES = tsetfsuid.c $(common_sources)
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c $(common_sources)
tfcntl_SOURCES = tfcntl.c $(common_sources)
topt_SOURCES = topt.c $(common_sources) 
tconf_SOURCES = tconf.c $(common_sources) 
tserialize_SOURCES = tserialize.c $(common_sources) 
tlist_SOURCES = tlist.c $(common_sources) 
tnpsrv_SOURCES = tnpsrv.c $(common_sources) 

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) t06.conf t08.conf
