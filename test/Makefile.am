check_PROGRAMS = \
	tsetuid tsetfsuid tsetfsuidsupp tsuppgrp tfcntl tstatfs trename tstat

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/diod/diod"
TESTS_ENVIRONMENT += "PATH_DIODCTL=$(top_builddir)/diodctl/diodctl"
TESTS_ENVIRONMENT += "PATH_DIODMOUNT=$(top_builddir)/diodmount/diodmount"
TESTS_ENVIRONMENT += "PATH_DIODCONF=$(top_builddir)/etc/diod.conf"
TESTS_ENVIRONMENT += "PATH_MNTDIR=$(shell readlink -f mnt.d)"
TESTS_ENVIRONMENT += "PATH_EXPDIR=$(shell readlink -f exp.d)"

TESTS = \
	t00-pthreads-setfsuid \
	t01-setfsuid-setgroups \
	t02-pthreads-setgroups \
	t03-pthreads-seteuid \
	t04-pthreads-fcntl \
	t05-mount-sanity \
	t06-read-sanity \
	t07-write-sanity \
	t08-option-parse \
	t09-mount-9P2000 \
	t10-mount-9P2000.u \
	t11-mount-9P2000.L \
	t12-mount-9P2000.H \
	t13-rename-across-dirs \
	t14-statfs \
	t15-fsync \
	t16-stat

$(TESTS): mnt.d exp.d

mnt.d exp.d:
	mkdir -p $@

CLEANFILES = *.out *.diff

AM_CFLAGS = @GCCWARN@

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
tfcntl_SOURCES = tfcntl.c $(common_sources)
tstatfs_SOURCES = tstatfs.c $(common_sources)
trename_SOURCES = trename.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)

clean: clean-am
	-rmdir mnt.d
	-rmdir exp.d
