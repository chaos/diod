check_PROGRAMS = \
	tfcntl \
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
	tfidpool

if LINUX
check_PROGRAMS += \
	tcap \
	tsetfsuid \
	tsetfsuidsupp
endif

TESTS = t02 t03 t04 t05 t06 t07 t08 t09 t10 t12 t13 t14 t15
if LINUX
TESTS += t00 t01 t11
endif
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


LDADD = $(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBLUA) $(LIBMUNGE) $(LIBCAP) $(LIBTCMALLOC)

common_sources = \
	$(top_srcdir)/utils/opt.c \
	$(top_srcdir)/utils/opt.h \
	test.h

diod_sources = \
	$(top_srcdir)/diod/ops.c \
	$(top_srcdir)/diod/ops.h \
	$(top_srcdir)/diod/fid.c \
	$(top_srcdir)/diod/fid.h \
	$(top_srcdir)/diod/exp.c \
	$(top_srcdir)/diod/exp.h \
	$(top_srcdir)/diod/ioctx.c \
	$(top_srcdir)/diod/ioctx.h \
	$(top_srcdir)/diod/xattr.c \
	$(top_srcdir)/diod/xattr.h

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
tnpsrv2_SOURCES = tnpsrv2.c $(common_sources) $(diod_sources)
tnpsrv3_SOURCES = tnpsrv3.c $(common_sources) $(diod_sources)
tlua_SOURCES = tlua.c $(common_sources) 
tcap_SOURCES = tcap.c $(common_sources) 

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) memcheck t06.conf t08.conf tcmalloc.supp
