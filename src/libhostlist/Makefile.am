AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir)

noinst_LIBRARIES = libhostlist.a

libhostlist_a_SOURCES = \
	errno_safe.h \
	util.h \
	util.c \
	hostname.h \
	hostname.c \
	hostrange.h \
	hostrange.c \
	hostlist.h \
	hostlist.c

test_ldadd = \
	$(builddir)/libhostlist.a \
	$(top_builddir)/src/libtap/libtap.a

TESTS = \
	test_util.t \
	test_hostname.t \
	test_hostrange.t \
	test_hostlist.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_util_t_SOURCES = test/util.c
test_util_t_LDADD = $(test_ldadd)

test_hostname_t_SOURCES = test/hostname.c
test_hostname_t_LDADD = $(test_ldadd)

test_hostrange_t_SOURCES = test/hostrange.c
test_hostrange_t_LDADD = $(test_ldadd)

test_hostlist_t_SOURCES = test/hostlist.c
test_hostlist_t_LDADD = $(test_ldadd)
