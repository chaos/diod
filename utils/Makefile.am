AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod -I../libnpclient

sbin_PROGRAMS = diodcat dtop diodload diodls diodshowmount dioddate
if LINUX
sbin_PROGRAMS += diodmount 
endif

common_ldadd = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBLUA) $(LIBMUNGE) $(LIBCAP) $(LIBCURSES) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC)

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

man8_MANS = \
	diodmount.8 \
	diodcat.8 \
	dtop.8 \
	diodload.8 \
	diodls.8 \
	diodshowmount.8 \
	dioddate.8
