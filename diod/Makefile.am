AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod

sbin_PROGRAMS = diod

diod_LDADD = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBLUA) $(LIBMUNGE) $(LIBCAP) \
	$(LIBIBVERBS) $(LIBRDMACM) $(LIBTCMALLOC)

diod_SOURCES = \
	diod.c \
	ops.c \
	ops.h \
	exp.c \
	exp.h \
	ioctx.c \
	ioctx.h \
	fid.c \
	fid.h

man8_MANS = \
        diod.8
