AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	$(CAP_CFLAGS) \
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

if MULTIUSER
if USE_GANESHA_KMOD
libnpfs_a_SOURCES += user-freebsd.c
else
libnpfs_a_SOURCES += user-linux.c
endif
else
libnpfs_a_SOURCES += user-stub.c
endif

if RDMA
libnpfs_a_SOURCES += rdmatrans.c
endif

test_ldadd = \
	$(builddir)/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libtest/libtest.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(CAP_LIBS) \
	$(LIBPTHREAD)

TESTS = \
	test_encoding.t \
	test_fidpool.t \
	test_setfsuid.t \
	test_setreuid.t

if MULTIUSER
TESTS += \
	test_capability.t
endif

check_PROGRAMS = $(TESTS)
TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_encoding_t_SOURCES = test/encoding.c
test_encoding_t_LDADD = $(test_ldadd)

test_fidpool_t_SOURCES = test/fidpool.c
test_fidpool_t_LDADD = $(test_ldadd)

if MULTIUSER
test_capability_t_SOURCES = test/capability.c
test_capability_t_LDADD = $(test_ldadd)
endif

test_setfsuid_t_SOURCES = test/setfsuid.c
test_setfsuid_t_LDADD = $(test_ldadd)

test_setreuid_t_SOURCES = test/setreuid.c
test_setreuid_t_LDADD = $(test_ldadd)
