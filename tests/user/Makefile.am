check_PROGRAMS = \
	conjoin \
	tattach \
	tattachmt \
	tmkdir \
	tread \
	tstat \
	twrite \
	tflush

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/diod/diod"
TESTS_ENVIRONMENT += "PATH_DIODCONF=$(top_builddir)/etc/diod.conf"
TESTS_ENVIRONMENT += "./runtest"

TESTS = t01 t02 t03 t04 t05 t06 t07 t08 t09 t10 t11 t12 t13 t15

$(TESTS): exp.d

exp.d:
	mkdir -p $@

CLEANFILES = *.out *.diff *.diod

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
        $(LIBWRAP) $(LIBPTHREAD) $(LIBLUA) $(LIBMUNGE) $(LIBCAP) $(LIBTCMALLOC)

common_sources =

conjoin_SOURCES = conjoin.c $(common_sources)
tattachmt_SOURCES = tattachmt.c $(common_sources)
tmkdir_SOURCES = tmkdir.c $(common_sources)
tread_SOURCES = tread.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)
twrite_SOURCES = twrite.c $(common_sources)
tflush_SOURCES = tflush.c $(common_sources)

clean: clean-am
	-rm -rf exp.d

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) runtest
