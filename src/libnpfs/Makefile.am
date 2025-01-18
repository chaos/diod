AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir)

noinst_LIBRARIES = libnpfs.a

libnpfs_a_SOURCES = \
	conn.c \
	error.c \
	fcall.c \
	fdtrans.c \
	fidpool.c \
	fmt.c \
	np.c \
	srv.c \
	trans.c \
	user.c \
	npstring.c \
	npfs.h \
	npfsimpl.h \
	protocol.h \
	types.h \
	ctl.c \
	xpthread.h

if USE_IMPERSONATION_LINUX
libnpfs_a_SOURCES += user-linux.c
else
if USE_IMPERSONATION_GANESHA
libnpfs_a_SOURCES += user-freebsd.c
else
libnpfs_a_SOURCES += user-stub.c
endif
endif

if RDMATRANS
libnpfs_a_SOURCES += rdmatrans.c
endif

test_ldadd = \
	$(builddir)/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libtest/libtest.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LIBCAP) \
	$(LIBPTHREAD)

TESTS = \
	test_encoding.t \
	test_fidpool.t \
	test_capability.t \
	test_setfsuid.t \
	test_setreuid.t

check_PROGRAMS = $(TESTS)
TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_encoding_t_SOURCES = test/encoding.c
test_encoding_t_LDADD = $(test_ldadd)

test_fidpool_t_SOURCES = test/fidpool.c
test_fidpool_t_LDADD = $(test_ldadd)

test_capability_t_SOURCES = test/capability.c
test_capability_t_LDADD = $(test_ldadd)

test_setfsuid_t_SOURCES = test/setfsuid.c
test_setfsuid_t_LDADD = $(test_ldadd)

test_setreuid_t_SOURCES = test/setreuid.c
test_setreuid_t_LDADD = $(test_ldadd)
