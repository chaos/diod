check_PROGRAMS =

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "MISC_SRCDIR=$(top_srcdir)/tests/misc"
TESTS_ENVIRONMENT += "MISC_BUILDDIR=$(top_builddir)/tests/misc"
TESTS_ENVIRONMENT += "TOP_SRCDIR=$(top_srcdir)"
TESTS_ENVIRONMENT += "TOP_BUILDDIR=$(top_builddir)"

TESTS = t15

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

EXTRA_DIST = $(TESTS) $(TESTS:%=%.exp) memcheck valgrind.supp
