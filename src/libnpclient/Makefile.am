AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	$(CAP_CFLAGS) \
	-I$(top_srcdir)

noinst_LIBRARIES = libnpclient.a

libnpclient_a_SOURCES = \
	fid.c \
	fsys.c \
	mount.c \
	npcimpl.h \
	npclient.h \
	open.c \
	pool.c \
	read.c \
	walk.c \
	write.c \
	mkdir.c \
	stat.c \
	remove.c \
	readdir.c \
	chmod.c \
	xattr.c \
	lock.c

test_ldadd = \
	$(builddir)/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LUA_LIB) \
	$(CAP_LIBS) \
	$(LIBPTHREAD)

TESTS = \
	test_simple.t

check_PROGRAMS = $(TESTS)
TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_simple_t_SOURCES = test/simple.c
test_simple_t_LDADD = $(test_ldadd)
