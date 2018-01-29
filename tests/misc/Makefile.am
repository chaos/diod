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
	tnpsrv \
	tnpsrv2 \
	tnpsrv3 \
	tlua \
	tcap \
	tfidpool

TESTS = t00 t01 t02 t03 t04 t05 t06 t07 t08 t09 t10 t11 t12 t13 t14 t15
# XFAIL_TESTS = t12

CLEANFILES = *.out *.diff

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I$(top_srcdir)/libnpfs \
        -I$(top_srcdir)/libnpclient \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod \
	-I$(top_srcdir)/utils \
	-I$(top_srcdir)/diod

LDADD = $(top_builddir)/utils/opt.o \
	$(top_builddir)/diod/ops.o \
	$(top_builddir)/diod/fid.o \
	$(top_builddir)/diod/exp.o \
	$(top_builddir)/diod/ioctx.o \
	$(top_builddir)/diod/xattr.o \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBLUA) $(LIBMUNGE) $(LIBCAP) $(LIBTCMALLOC)

tsetuid_SOURCES = tsetuid.c
tsuppgrp_SOURCES = tsuppgrp.c
tsetfsuid_SOURCES = tsetfsuid.c
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c
tfcntl_SOURCES = tfcntl.c
topt_SOURCES = topt.c
tconf_SOURCES = tconf.c
tserialize_SOURCES = tserialize.c
tlist_SOURCES = tlist.c
tnpsrv_SOURCES = tnpsrv.c
tnpsrv2_SOURCES = tnpsrv2.c
tnpsrv3_SOURCES = tnpsrv3.c
tlua_SOURCES = tlua.c
tcap_SOURCES = tcap.c

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) memcheck t06.conf t08.conf valgrind.supp
