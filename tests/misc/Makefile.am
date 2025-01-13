check_PROGRAMS = \
	tsetfsuidsupp \
	tsetuid \
	tsuppgrp \
	tconf \
	tnpsrv \
	tnpsrv2 \
	tnpsrv3 \
	tlua

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "MISC_SRCDIR=$(top_srcdir)/tests/misc"
TESTS_ENVIRONMENT += "MISC_BUILDDIR=$(top_builddir)/tests/misc"
TESTS_ENVIRONMENT += "TOP_SRCDIR=$(top_srcdir)"
TESTS_ENVIRONMENT += "TOP_BUILDDIR=$(top_builddir)"

TESTS = t01 t02 t03 t06 t08 t10 t13 t14 t15

CLEANFILES = *.out *.diff

AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
        -I$(top_srcdir)
	$(LUA_INCLUDE)

LDADD = $(top_builddir)/src/cmd/opt.o \
	$(top_builddir)/src/daemon/ops.o \
	$(top_builddir)/src/daemon/fid.o \
	$(top_builddir)/src/daemon/exp.o \
	$(top_builddir)/src/daemon/ioctx.o \
	$(top_builddir)/src/daemon/xattr.o \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LUA_LIB) $(LIBMUNGE) $(LIBCAP) $(LIBTCMALLOC)

common_sources = test.h

tsetuid_SOURCES = tsetuid.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tsetfsuidsupp_SOURCES = tsetfsuidsupp.c $(common_sources)
tconf_SOURCES = tconf.c $(common_sources)
tnpsrv_SOURCES = tnpsrv.c $(common_sources)
tnpsrv2_SOURCES = tnpsrv2.c $(common_sources)
tnpsrv3_SOURCES = tnpsrv3.c $(common_sources)
tlua_SOURCES = tlua.c $(common_sources)

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) memcheck t06.conf t08.conf valgrind.supp
