##
# Prologue
##
AC_INIT(m4_esyscmd([awk '/Name:/ {printf "%s",$2; exit}' META]),
        m4_esyscmd([awk '/Version:/ {printf "%s",$2; exit}' META]))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_SYSTEM
X_AC_META
X_AC_EXPAND_INSTALL_DIRS

##
# Automake support
##
AM_INIT_AUTOMAKE($META_NAME, $META_VERSION)
AM_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

##
# Checks for programs
##
AC_PROG_CC
if test "$GCC" = yes; then
  GCCWARN="-Wall"
  AC_SUBST([GCCWARN])
fi
AC_PROG_RANLIB

# dbench is conditionally compiled for 'make check'
DBENCH

#will we be able to do a diodmount?
AM_CONDITIONAL([LINUX], [test `uname` = Linux])

##
# Checks for header files.
##
AC_HEADER_STDC
AC_CHECK_HEADERS( \
  getopt.h \
  pthread.h \
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
X_AC_CHECK_PTHREADS
X_AC_WRAP
X_AC_CURSES
X_AC_CHECK_COND_LIB(munge, munge_ctx_create)
X_AC_CHECK_COND_LIB(cap, cap_get_proc)
X_AC_TCMALLOC
AX_LUA_HEADERS
AX_LUA_HEADERS(501)
AX_LUA_LIBS
X_AC_RDMATRANS

##
# For list.c, hostlist.c, hash.c
##
AC_DEFINE(WITH_LSD_FATAL_ERROR_FUNC, 1, [Define lsd_fatal_error])
AC_DEFINE(WITH_LSD_NOMEM_ERROR_FUNC, 1, [Define lsd_nomem_error])
AC_DEFINE(WITH_PTHREADS, 1, [Make liblsd thread safe])

AC_SYS_LARGEFILE

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  diod.spec \
  libdiod/Makefile \
  liblsd/Makefile \
  libnpfs/Makefile \
  libnpclient/Makefile \
  diod/diod.8 \
  utils/diodmount.8 \
  utils/diodcat.8 \
  utils/dtop.8 \
  utils/diodload.8 \
  utils/diodls.8 \
  utils/diodshowmount.8 \
  utils/dioddate.8 \
  etc/diod.conf.5 \
  scripts/Makefile \
  scripts/diod.init \
  scripts/auto.diod \
  diod/Makefile \
  utils/Makefile \
  tests/Makefile \
  tests/misc/Makefile \
  tests/kern/Makefile \
  tests/kern/fstest/Makefile \
  tests/kern/dbench/Makefile \
  tests/kern/postmark/Makefile \
  tests/kern/fsx/Makefile \
  tests/kern/fsstress/Makefile \
  tests/user/Makefile \
  etc/Makefile \
)

AC_OUTPUT
