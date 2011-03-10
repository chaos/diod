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
	tconfig \
	tfsgid

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/diod/diod"
TESTS_ENVIRONMENT += "PATH_DIODCTL=$(top_builddir)/diodctl/diodctl"
TESTS_ENVIRONMENT += "PATH_DIODMOUNT=$(top_builddir)/diodmount/diodmount"
TESTS_ENVIRONMENT += "PATH_DIODCONF=$(top_builddir)/etc/diod.conf"
TESTS_ENVIRONMENT += "PATH_MNTDIR=$(shell readlink -f mnt.d)"
TESTS_ENVIRONMENT += "PATH_EXPDIR=$(shell readlink -f exp.d)"
TESTS_ENVIRONMENT += "./runtest"

TESTS = t05 t06 t07 t13 t14 t15 t16 t17 t18 t19 t20 t21 t22 \
	t23 t24 t25 t26 

XFAIL_TESTS = t16 t17
if ! LARGEIO
XFAIL_TESTS += t20 t21
endif

$(TESTS): mnt.d exp.d

mnt.d exp.d:
	mkdir -p $@

CLEANFILES = *.out *.diff *.stats *.diod

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I../libnpfs -I../liblsd -I../libdiod


LDADD = $(top_builddir)/libdiod/libdiod.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = test.h

kconjoin_SOURCES = kconjoin.c $(common_sources)
tsuppgrp_SOURCES = tsuppgrp.c $(common_sources)
tstatfs_SOURCES = tstatfs.c $(common_sources)
trename_SOURCES = trename.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)
tflock_SOURCES = tflock.c $(common_sources)
tfsgid_SOURCES = tfsgid.c $(common_sources)

clean: clean-am
	-rmdir mnt.d
	-rmdir exp.d
