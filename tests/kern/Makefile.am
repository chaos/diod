check_PROGRAMS = \
	kconjoin \
	tsuppgrp \
	tstatfs \
	trename \
	tstat \
	tflock \
	tflock2 \
	tfcntl2 \
	tfcntl3 \
	tfsgid \
	tcreate \
	tatomic \
	pathwalk

SUBDIRS = fstest postmark fsx fsstress
if DBENCH
SUBDIRS += dbench
endif

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/diod/diod"
TESTS_ENVIRONMENT += "PATH_DIODMOUNT=$(top_builddir)/utils/diodmount"
TESTS_ENVIRONMENT += "KERN_SRCDIR=$(top_srcdir)/tests/kern"
TESTS_ENVIRONMENT += "KERN_BUILDDIR=$(top_builddir)/tests/kern"
TESTS_ENVIRONMENT += "TOP_SRCDIR=$(top_srcdir)"
TESTS_ENVIRONMENT += "TOP_BUILDDIR=$(top_builddir)"
TESTS_ENVIRONMENT += "${srcdir}/runtest"

TESTS = t05 t06 t07 t12 t13 t14 t15 t16 t17 t18 t19 t22 \
	t23 t24 t25 t26 t27 t28 t29 t30 t31 t32 t33 \
	t34 t35 t36 t37 t38 t39 t40 t41 t42 t43 t44 t45 t46 t47

XFAIL_TESTS = t48 t49

CLEANFILES = *.out *.diff *.diod *.postmark *.dbench

AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
        -I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod \
	$(LUA_INCLUDE)


LDADD = $(top_builddir)/libdiod/libdiod.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LUA_LIB) $(LIBMUNGE) $(LIBCAP) $(LIBTCMALLOC)

common_sources = test.h

kconjoin_SOURCES = kconjoin.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tstatfs_SOURCES = tstatfs.c $(common_sources)
trename_SOURCES = trename.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)
tflock_SOURCES = tflock.c $(common_sources)
tfsgid_SOURCES = tfsgid.c $(common_sources)
tcreate_SOURCES = tcreate.c $(common_sources)
tatomic_SOURCES = tatomic.c $(common_sources)
pathwalk_SOURCES = pathwalk.c $(common_sources)

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) runtest t00
