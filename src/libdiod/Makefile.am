AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(LUA_INCLUDE)

noinst_LIBRARIES = libdiod.a

libdiod_a_SOURCES = \
	diod_auth.c \
	diod_auth.h \
	diod_log.c \
	diod_log.h \
	diod_conf.c \
	diod_conf.h \
	diod_sock.c \
	diod_sock.h \
	diod_fid.c \
	diod_fid.h \
	diod_ioctx.c \
	diod_ioctx.h \
	diod_xattr.c \
	diod_xattr.h \
	diod_exp.c \
	diod_exp.h \
	diod_ops.c \
	diod_ops.h \
	lsderr.c

if RDMATRANS
libdiod_a_SOURCES += diod_rdma.c diod_rdma.h
endif

test_ldadd = \
	$(top_builddir)/src/libtest/libtest.a \
	$(builddir)/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(builddir)/libdiod.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LUA_LIB) \
	$(LIBCAP) \
	$(LIBMUNGE) \
	$(LIBWRAP) \
	$(LIBPTHREAD)

TESTS = \
	test_configfile.t \
	test_threadwrlock.t \
	test_read.t \
	test_directory.t \
	test_multiuser.t

check_PROGRAMS = $(TESTS)
TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_configfile_t_SOURCES = test/configfile.c
test_configfile_t_LDADD = $(test_ldadd)

test_threadwrlock_t_SOURCES = test/threadwrlock.c
test_threadwrlock_t_LDADD = $(test_ldadd)

test_read_t_SOURCES = test/read.c
test_read_t_LDADD = $(test_ldadd)

test_directory_t_SOURCES = test/directory.c
test_directory_t_LDADD = $(test_ldadd)

test_multiuser_t_SOURCES = test/multiuser.c
test_multiuser_t_LDADD = $(test_ldadd)
