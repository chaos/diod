check_PROGRAMS = \
	kconjoin \
	tsuppgrp \
	tstatfs \
	trename \
	tstat \
	tflock \
	tflock2 \
	tfsgid \
	tcreate \
	tatomic \
	pathwalk

SUBDIRS = fstest fsx fsstress

AM_TESTS_ENVIRONMENT = \
	PATH_DIOD='$(top_builddir)/src/cmd/diod'; export PATH_DIOD; \
	PATH_MOUNT_DIOD='$(top_builddir)/src/cmd/mount.diod'; export PATH_MOUNT_DIOD; \
	KERN_SRCDIR='$(top_srcdir)/tests/kern'; export KERN_SRCDIR; \
	KERN_BUILDDIR='$(top_builddir)/tests/kern'; export KERN_BUILDDIR; \
	TOP_SRCDIR='$(top_srcdir)'; export TOP_SRCDIR; \
	TOP_BUILDDIR='$(top_builddir)'; export TOP_BUILDDIR;

LOG_COMPILER = ${srcdir}/runtest

TESTS = t12 t13 t14 t15 t16 t17 t18 \
	t24 t25 t31 \
	t37 t38 t39 t42 t43 t44 t45 t46 t47

XFAIL_TESTS =

CLEANFILES = *.out *.diff *.diod

AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(MUNGE_CFLAGS) \
	$(CAP_CFLAGS) \
	$(LUA_INCLUDE)


LDADD = $(top_builddir)/src/libdiod/libdiod.a \
        $(top_builddir)/src/libnpfs/libnpfs.a \
        $(top_builddir)/src/liblsd/liblsd.a \
        $(LIBPTHREAD) $(LUA_LIB) $(MUNGE_LIBS) $(CAP_LIBS) $(LIBTCMALLOC)

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

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) runtest
