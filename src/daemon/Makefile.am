AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(LUA_INCLUDE)

sbin_PROGRAMS = diod

diod_LDADD = \
	$(top_builddir)/src/libdiod/libdiod.a \
	$(top_builddir)/src/libnpclient/libnpclient.a \
	$(top_builddir)/src/libnpfs/libnpfs.a \
	$(top_builddir)/src/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LUA_LIB) $(LIBMUNGE) $(LIBCAP) \
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
	fid.h \
	xattr.c \
	xattr.h
