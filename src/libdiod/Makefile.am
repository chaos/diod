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
	lsderr.c

if RDMATRANS
libdiod_a_SOURCES += diod_rdma.c diod_rdma.h
endif

test_ldadd = \
	$(builddir)/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(builddir)/libdiod.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LUA_LIB) \
	$(LIBCAP) \
	$(LIBPTHREAD)

TESTS = \
	test_configfile.t

check_PROGRAMS = $(TESTS)
TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_configfile_t_SOURCES = test/configfile.c
test_configfile_t_LDADD = $(test_ldadd)
