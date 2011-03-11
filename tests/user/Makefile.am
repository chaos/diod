check_PROGRAMS = \
	conjoin \
	tattach \
	tmkdir \
	tread \
	tstat \
	twrite

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/diod/diod"
TESTS_ENVIRONMENT += "PATH_DIODCTL=$(top_builddir)/diodctl/diodctl"
TESTS_ENVIRONMENT += "PATH_DIODMOUNT=$(top_builddir)/diodmount/diodmount"
TESTS_ENVIRONMENT += "PATH_DIODCONF=$(top_builddir)/etc/diod.conf"
TESTS_ENVIRONMENT += "PATH_EXPDIR=$(shell readlink -f exp.d)"
TESTS_ENVIRONMENT += "./runtest"

TESTS = t01 t02 t03 t04 t05 t06 t07 t08 t09

$(TESTS): exp.d

exp.d:
	mkdir -p $@

CLEANFILES = *.out *.diff *.stats *.diod

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
        -I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/libnpclient \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod


LDADD = $(top_builddir)/libdiod/libdiod.a \
        $(top_builddir)/libnpclient/libnpclient.a \
        $(top_builddir)/libnpfs/libnpfs.a \
        $(top_builddir)/liblsd/liblsd.a \
        $(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources =

conjoin_SOURCES = conjoin.c $(common_sources)
tattach_SOURCES = tattach.c $(common_sources)
tmkdir_SOURCES = tmkdir.c $(common_sources)
tread_SOURCES = tread.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)
twrite_SOURCES = twrite.c $(common_sources)

clean: clean-am
	-rmdir exp.d

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) runtest
