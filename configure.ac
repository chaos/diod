##
# Prologue
##
AC_INIT([diod],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_HOST
AC_REQUIRE_AUX_FILE([tap-driver.sh])
X_AC_EXPAND_INSTALL_DIRS

##
# Automake support
##
AM_INIT_AUTOMAKE([subdir-objects foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([config/config.h])
AM_MAINTAINER_MODE([enable])

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

##
# Checks for programs
##
AC_PROG_CC
AM_PROG_CC_C_O
AX_COMPILER_VENDOR
AS_CASE($ax_cv_c_compiler_vendor,
  [gnu], [
      WARNING_CFLAGS="-Wall -Werror"
  ],
  [clang], [
      WARNING_CFLAGS="-Wall -Werror -Wno-unknown-warning-option -Wno-error=unknown-warning-option"
  ]
)
AC_SUBST([WARNING_CFLAGS])

##
# Fix non-optional LUAJIT support macro in autoconf-archive > 2023.02.20
##
AC_ARG_WITH(luajit, [AS_HELP_STRING([--with-luajit],
            [Prefer LuaJIT over PUC Lua, even if the latter is newer. Default: no])
])
AM_CONDITIONAL([LUAJIT], [test "x$with_luajit" != 'xno'])


AC_PROG_RANLIB

##
# Checks for pkg.m4 from pkgconf package
##
m4_ifndef([PKG_PROG_PKG_CONFIG],
  [AC_MSG_ERROR([PKG_PROG_PKG_CONFIG not found, please install pkgconf package before configuring.])])

##
# Initialize pkg-config for PKG_CHECK_MODULES to avoid conditional issues
##
PKG_PROG_PKG_CONFIG

##
# Checks for ncurses
##
PKG_CHECK_MODULES([ncurses], [ncurses])

##
# Checks for header files.
##
AC_CHECK_HEADERS( \
  pthread.h \
  sys/prctl.h \
  sys/statfs.h \
  sys/sysmacros.h \
  sys/xattr.h \
)

##
# Checks for typedefs, structures, and compiler characteristics
##
AC_C_BIGENDIAN
AC_C_CONST

##
# Checks for library functions
##
AC_CHECK_FUNCS( \
  utimensat \
)
AC_FUNC_STRERROR_R
X_AC_CHECK_PTHREADS
X_AC_TCMALLOC
X_AC_RDMA

##
# For list.c, hostlist.c, hash.c
##
AC_DEFINE(WITH_LSD_FATAL_ERROR_FUNC, 1, [Define lsd_fatal_error])
AC_DEFINE(WITH_LSD_NOMEM_ERROR_FUNC, 1, [Define lsd_nomem_error])
AC_DEFINE(WITH_PTHREADS, 1, [Make liblsd thread safe])

AC_SYS_LARGEFILE

##
# Optional features
##
AC_ARG_ENABLE([diodmount],
  [AS_HELP_STRING([--disable-diodmount], [do not build diodmount])])

AC_ARG_ENABLE([multiuser],
  [AS_HELP_STRING([--disable-multiuser], [build without multi-user support])])

AC_ARG_ENABLE([auth],
  [AS_HELP_STRING([--disable-auth], [build without authentication support])])

AC_ARG_WITH([ganesha-kmod],
  [AS_HELP_STRING([--with-ganesha-kmod], [use nfs-ganesha-kmod syscalls for multi-user])])

AS_IF([test "x$with_ganesha_kmod" = "xyes"], [
  AC_DEFINE([USE_GANESHA_KMOD], [1], [Use nfs-ganesha-kmod syscalls])
])

AS_IF([test "x$enable_auth" != "xno"], [
  PKG_CHECK_MODULES([MUNGE], [munge], [], [
    AC_MSG_ERROR([Install munge or configure with --disable-auth])
  ])
  AC_DEFINE([AUTH], [1], [Support MUNGE authentication])
])

AS_IF([test "x$enable_multiuser" != "xno"], [
  PKG_CHECK_MODULES([CAP], [libcap], [], [
    AC_MSG_ERROR([Install libcap or configure with --disable-multiuser])
  ])
  AC_DEFINE([MULTIUSER], [1], [service files to multiple users])
])

AC_ARG_ENABLE([config],
  [AS_HELP_STRING([--disable-config], [disable lua config file support])])

AS_IF([test "x$enable_config" != "xno"], [
  AX_PROG_LUA([5.1],[5.5])
  AX_LUA_HEADERS([],[
    AC_MSG_ERROR([Install lua headers or configure with --disable-config])
  ])
  AX_LUA_LIBS([],[
    AC_MSG_ERROR([Install lua libs or configure with --disable-config])
  ])
  AC_DEFINE([HAVE_CONFIG_FILE], [1], [lua config file support])
])

AM_CONDITIONAL([ENABLE_DIODMOUNT], [test "x${enable_diodmount}" != "xno"])
AM_CONDITIONAL([MULTIUSER], [test "x${enable_multiuser}" != "xno"])
AM_CONDITIONAL([USE_GANESHA_KMOD], [test "x${with_ganesha_kmod}" = "xyes"])

##
# Check for systemd
##
RRA_WITH_SYSTEMD_UNITDIR

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  diod.spec \
  src/libtap/Makefile \
  src/libtest/Makefile \
  src/libdiod/Makefile \
  src/liblsd/Makefile \
  src/libnpfs/Makefile \
  src/libnpclient/Makefile \
  man/Makefile \
  man/diod.8 \
  man/diodmount.8 \
  man/diodcat.8 \
  man/dtop.8 \
  man/diodload.8 \
  man/diodls.8 \
  man/diodshowmount.8 \
  man/dioddate.8 \
  man/diod.conf.5 \
  etc/diod.service \
  etc/auto.diod \
  src/cmd/Makefile \
  src/Makefile \
  t/Makefile \
  tests/Makefile \
  tests/kern/Makefile \
  tests/kern/fstest/Makefile \
  tests/kern/postmark/Makefile \
  tests/kern/fsx/Makefile \
  tests/kern/fsstress/Makefile \
  etc/Makefile \
)

AC_OUTPUT
