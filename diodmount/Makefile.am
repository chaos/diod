AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../npfs/include -I../liblsd -I../common

sbin_PROGRAMS = diodmount

diodmount_LDADD = \
	$(top_builddir)/common/libdiod.a \
	$(top_builddir)/npfs/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

diodmount_SOURCES = \
	diodmount.c \
	opt.c \
	opt.h
