AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/liblsd \
	-I$(top_srcdir)/libdiod \
	-I$(top_srcdir)/libnpclient \
	$(LUA_INCLUDE)
AM_CPPFLAGS += $(ncurses_CPPFLAGS)

sbin_PROGRAMS = diodcat dtop diodload diodls diodshowmount dioddate

if ENABLE_DIODMOUNT
sbin_PROGRAMS += diodmount
endif

common_ldadd = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LUA_LIB) $(LIBMUNGE) $(LIBCAP) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC) $(ncurses_LIBS)

common_sources = \
	opt.c \
	opt.h

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
