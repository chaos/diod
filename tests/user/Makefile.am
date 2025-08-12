check_PROGRAMS = \
	conjoin \
	tattach \
	tmkdir \
	tread \
	tstat \
	twrite \
	tgetxattr \
	tsetxattr \
	tsetxattr_wildoffset \
	tremovexattr \
	txattr \
	testopenfid

AM_TESTS_ENVIRONMENT = \
	PATH_DIOD='$(top_builddir)/src/cmd/diod'; export PATH_DIOD; \
	PATH_DIODCONF='$(top_builddir)/etc/diod.conf'; export PATH_DIODCONF; \
	USER_SRCDIR='$(top_srcdir)/tests/user'; export USER_SRCDIR; \
	USER_BUILDDIR='$(top_builddir)/tests/user'; export USER_BUILDDIR;

LOG_COMPILER = ${srcdir}/runtest

AM_LOG_FLAGS =

TESTS = t01 t02 t03 t04 t05 t06 t07 t08 t09 t12 t16 t17 t18 t19 t20

$(TESTS): exp.d

exp.d:
	mkdir -p $@

CLEANFILES = *.out *.diff *.diod

AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(MUNGE_CFLAGS) \
	$(CAP_CFLAGS) \
	$(LUA_INCLUDE)


LDADD = $(top_builddir)/src/libdiod/libdiod.a \
        $(top_builddir)/src/libnpclient/libnpclient.a \
        $(top_builddir)/src/libnpfs/libnpfs.a \
        $(top_builddir)/src/liblsd/liblsd.a \
        $(LIBPTHREAD) $(LUA_LIB) $(MUNGE_LIBS) $(CAP_LIBS) $(LIBTCMALLOC)

common_sources =

conjoin_SOURCES = conjoin.c $(common_sources)
tmkdir_SOURCES = tmkdir.c $(common_sources)
tread_SOURCES = tread.c $(common_sources)
tstat_SOURCES = tstat.c $(common_sources)
twrite_SOURCES = twrite.c $(common_sources)
tgetxattr_SOURCES = tgetxattr.c $(common_sources)
tsetxattr_SOURCES = tsetxattr.c $(common_sources)
tremovexattr_SOURCES = tremovexattr.c $(common_sources)
testopenfid_SOURCES = testopenfid.c $(common_sources)

clean: clean-am
	-rm -rf exp.d

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) runtest
