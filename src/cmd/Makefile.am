AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(MUNGE_CFLAGS) \
	$(LUA_INCLUDE)
AM_CPPFLAGS += $(ncurses_CPPFLAGS)

sbin_PROGRAMS = diod diodcat dtop diodload diodls diodshowmount dioddate

if ENABLE_DIODMOUNT
sbin_PROGRAMS += diodmount
endif

common_ldadd = \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(LIBPTHREAD) $(LUA_LIB) $(MUNGE_LIBS) $(LIBCAP) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC) $(ncurses_LIBS)

common_sources = \
	opt.c \
	opt.h

diod_LDADD = $(common_ldadd)
diod_SOURCES = diod.c $(common_sources)

diodmount_LDADD = $(common_ldadd)
diodmount_SOURCES = diodmount.c $(common_sources)

diodcat_LDADD = $(common_ldadd)
diodcat_SOURCES = diodcat.c $(common_sources)

dtop_LDADD = $(common_ldadd)
dtop_SOURCES = dtop.c sample.c sample.h

diodload_LDADD = $(common_ldadd)
diodload_SOURCES = diodload.c $(common_sources)

diodls_LDADD = $(common_ldadd)
diodls_SOURCES = diodls.c $(common_sources)

diodshowmount_LDADD = $(common_ldadd)
diodshowmount_SOURCES = diodshowmount.c $(common_sources)

dioddate_LDADD = $(common_ldadd)
dioddate_SOURCES = dioddate.c $(common_sources)

TESTS = \
	test_opt.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_opt_t_SOURCES = test/opt.c
test_opt_t_LDADD = \
	$(builddir)/opt.o \
	$(common_ldadd) \
	$(top_builddir)/src/libtap/libtap.a
