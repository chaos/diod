AM_CFLAGS = @WARNING_CFLAGS@ \
	-Wno-parentheses -Wno-error=parentheses

AM_CPPFLAGS = \
	-I$(top_srcdir)

noinst_LIBRARIES = liblsd.a

liblsd_a_SOURCES = \
	hostlist.c \
	hostlist.h \
	list.c \
	list.h \
	hash.c \
	hash.h \
	thread.c \
	thread.h \
	error.c

test_ldadd = \
	$(builddir)/liblsd.a \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libtap/libtap.a \
	$(LIBPTHREAD)

TESTS = \
	test_list.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_list_t_SOURCES = test/list.c
test_list_t_LDADD = $(test_ldadd)
