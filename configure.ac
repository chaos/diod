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
AM_INIT_AUTOMAKE([subdir-objects foreign]
		m4_esyscmd([case `automake --version | head -n 1` in
                             *1.11*);;
                             *) echo serial-tests;;
                             esac]))
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

AC_PROG_RANLIB

##
# Checks for pkg.m4 from pkgconf package
##
m4_ifndef([PKG_PROG_PKG_CONFIG],
  [AC_MSG_ERROR([PKG_PROG_PKG_CONFIG not found, please install pkgconf package before configuring.])])

##
# Checks for ncurses
##
PKG_CHECK_MODULES([ncurses], [ncurses])

##
# Checks for header files.
##
AC_CHECK_HEADERS( \
  getopt.h \
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
  getopt_long \
  vsnprintf \
  vsscanf \
  utimensat \
)
AC_FUNC_STRERROR_R
X_AC_CHECK_PTHREADS
X_AC_WRAP
X_AC_CHECK_COND_LIB(munge, munge_ctx_create)
X_AC_CHECK_COND_LIB(cap, cap_get_proc)
X_AC_TCMALLOC
X_AC_RDMATRANS

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
  [AS_HELP_STRING([--disable-diodmount], [do not build diodmount])],
  [],
  [enable_diodmount=auto])
AC_ARG_ENABLE([tests],
  [AS_HELP_STRING([--disable-tests], [do not build tests])],
  [],
  [enable_tests=auto])
AC_ARG_ENABLE([impersonation],
  [AS_HELP_STRING([--enable-impersonation], [allow access=user])],
  [],
  [enable_impersonation=auto])

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

case "${host_os}" in
  linux*)
    case "${enable_diodmount}" in
      no)
        ;;
      *)
        enable_diodmount=yes;;
    esac
    case "${enable_tests}" in
      no)
        ;;
      *)
        enable_tests=yes;;
    esac
    case "${enable_impersonation}" in
      no)
        ;;
      yes|linux|auto)
        enable_impersonation=linux;;
      *)
        AC_MSG_FAILURE([unsupported impersonation model]);;
    esac
    ;;
  freebsd*)
    case "${enable_diodmount}" in
      yes)
        ;;
      *)
        enable_diodmount=no;;
    esac
    case "${enable_tests}" in
      yes)
        ;;
      *)
        enable_tests=no;;
    esac
    case "${enable_impersonation}" in
      no|auto)
        enable_impersonation=no;;
      yes|ganesha)
        enable_impersonation=ganesha;;
      *)
        AC_MSG_FAILURE([unsupported impersonation model]);;
    esac
    ;;
  *)
    case "${enable_diodmount}" in
      yes)
        ;;
      *)
        enable_diodmount=no;;
    esac
    case "${enable_tests}" in
      yes)
        ;;
      *)
        enable_tests=no;;
    esac
    case "${enable_impersonation}" in
      no|auto)
        enable_impersonation=no;;
      *)
        AC_MSG_FAILURE([unsupported impersonation model]);;
    esac
    ;;
esac

AM_CONDITIONAL([ENABLE_DIODMOUNT], [test "x${enable_diodmount}" = "xyes"])
AM_CONDITIONAL([ENABLE_TESTS], [test "x${enable_tests}" = "xyes"])
AM_CONDITIONAL([USE_IMPERSONATION_LINUX], [test "x${enable_impersonation}" = "xlinux"])
AM_CONDITIONAL([USE_IMPERSONATION_GANESHA], [test "x${enable_impersonation}" = "xganesha"])

if test "x${enable_impersonation}" = "xlinux"; then
  AC_DEFINE([USE_IMPERSONATION_LINUX], [1], [Use Linux setfsuid])
fi
if test "x${enable_impersonation}" = "xganesha"; then
  AC_DEFINE([USE_IMPERSONATION_GANESHA], [1], [Use nfs-ganesha-kmod syscalls])
fi

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
  src/daemon/Makefile \
  src/cmd/Makefile \
  src/Makefile \
  tests/Makefile \
  tests/misc/Makefile \
  tests/kern/Makefile \
  tests/kern/fstest/Makefile \
  tests/kern/postmark/Makefile \
  tests/kern/fsx/Makefile \
  tests/kern/fsstress/Makefile \
  tests/user/Makefile \
  etc/Makefile \
)

AC_OUTPUT
