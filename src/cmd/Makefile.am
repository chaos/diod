AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(MUNGE_CFLAGS) \
	$(CAP_CFLAGS) \
	$(LUA_INCLUDE)
AM_CPPFLAGS += $(ncurses_CPPFLAGS)

sbin_PROGRAMS = \
	diod \
	diodcli \
	dtop

if ENABLE_DIODMOUNT
sbin_PROGRAMS += mount.diod
endif

common_ldadd = \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(LIBPTHREAD) $(LUA_LIB) $(MUNGE_LIBS) $(CAP_LIBS) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC) $(ncurses_LIBS)

common_sources = \
	opt.c \
	opt.h

diod_LDADD = $(common_ldadd)
diod_SOURCES = diod.c $(common_sources)

diodcli_LDADD = $(common_ldadd)
diodcli_SOURCES = diodcli.c $(common_sources)

mount_diod_LDADD = $(common_ldadd)
mount_diod_SOURCES = mount.diod.c $(common_sources)

dtop_LDADD = $(common_ldadd)
dtop_SOURCES = dtop.c sample.c sample.h

TESTS = \
	test_opt.t

check_PROGRAMS = \
	test_diodrun \
	test_tflush \
	test_tattach \
	test_loadgen \
	test_pathwalk \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_opt_t_SOURCES = test/opt.c
test_opt_t_LDADD = \
	$(builddir)/opt.o \
	$(common_ldadd) \
	$(top_builddir)/src/libtap/libtap.a

test_diodrun_SOURCES = test/diodrun.c $(common_sources)
test_diodrun_LDADD = \
	$(common_ldadd)

test_tflush_SOURCES = test/tflush.c $(common_sources)
test_tflush_LDADD = \
	$(common_ldadd)

test_tattach_SOURCES = test/tattach.c $(common_sources)
test_tattach_LDADD = \
	$(common_ldadd)

test_loadgen_SOURCES = test/loadgen.c $(common_sources)
test_loadgen_LDADD = \
	$(common_ldadd)

test_pathwalk_SOURCES = test/pathwalk.c $(common_sources)
test_pathwalk_LDADD = \
	$(common_ldadd)
