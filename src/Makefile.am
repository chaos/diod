AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../npfs/include -I../liblsd

sbin_PROGRAMS = \
	diod

diod_LDADD = \
	$(top_builddir)/npfs/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA)


diod_SOURCES = \
	diod.c \
	diod_log.c \
	diod_log.h \
	diod_conf.c \
	diod_conf.h \
	diod_ops.c \
	diod_ops.h \
	diod_trans.c \
	diod_trans.h \
	diod_upool.c \
	diod_upool.h 
