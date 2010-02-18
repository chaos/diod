AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../npfs/include -I../liblsd

sbin_PROGRAMS = \
	diod \
	diodctl \
	diodmount

common_ldadd = \
	$(top_builddir)/npfs/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

diod_LDADD = \
	$(common_ldadd)

diodctl_LDADD = \
	$(common_ldadd)

common_sources = \
	diod_log.c \
	diod_log.h \
	diod_conf.c \
	diod_conf.h \
	diod_ops.c \
	diod_ops.h \
	diod_trans.c \
	diod_trans.h \
	diod_upool.c \
	diod_upool.h \
	diod_sock.c \
	diod_sock.h 

diod_SOURCES = \
	$(common_sources) \
	diod.c

diodctl_SOURCES = \
	$(common_sources) \
	diod_ctl.c

diodmount_SOURCES = \
	diod_mount.c
