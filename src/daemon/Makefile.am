AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(LUA_INCLUDE)

sbin_PROGRAMS = diod

diod_LDADD = \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LUA_LIB) $(LIBMUNGE) $(LIBCAP) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC)

diod_SOURCES = \
	diod.c \
	ops.c \
	ops.h \
	exp.c \
	exp.h \
	ioctx.c \
	ioctx.h \
	fid.c \
	fid.h \
	xattr.c \
	xattr.h

test_ldadd = \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LIBPTHREAD)

xtest_ldadd = \
	$(builddir)/ops.o \
	$(builddir)/fid.o \
	$(builddir)/exp.o \
	$(builddir)/ioctx.o \
	$(builddir)/xattr.o \
	$(test_ldadd) \
	$(LUA_LIB) \
	$(LIBCAP) \
	$(LIBMUNGE) \
	$(LIBWRAP)

TESTS = \
	test_threadwrlock.t \
	test_read.t \
	test_directory.t \
	test_multiuser.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_threadwrlock_t_SOURCES = test/threadwrlock.c
test_threadwrlock_t_LDADD = $(test_ldadd)

test_read_t_SOURCES = test/read.c
test_read_t_LDADD = $(xtest_ldadd)

test_directory_t_SOURCES = test/directory.c
test_directory_t_LDADD = $(xtest_ldadd)

test_multiuser_t_SOURCES = test/multiuser.c
test_multiuser_t_LDADD = $(xtest_ldadd)
