AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod -I../libnpclient

sbin_PROGRAMS = diodmount diodcat dtop diodload

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

man8_MANS = \
	diodmount.8 \
	diodcat.8 \
	dtop.8 \
	diodload.8
