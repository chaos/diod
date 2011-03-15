AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod -I../libnpclient

sbin_PROGRAMS = diodmount diodexp

common_ldadd = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpclient/libnpclient.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

common_sources = \
	util.c \
	util.h \
	ctl.c \
	ctl.h \
	opt.c \
	opt.h

diodmount_LDADD = $(common_ldadd)
diodmount_SOURCES = diodmount.c $(common_sources)

diodexp_LDADD = $(common_ldadd)
diodexp_SOURCES = diodexp.c $(common_sources)

man8_MANS = \
        diodmount.8
