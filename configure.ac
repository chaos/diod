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

##
# Checks for programs
##
AC_PROG_CC
if test "$GCC" = yes; then
  GCCWARN="-Wall -Werror"
  AC_SUBST([GCCWARN])
  AC_SUBST([GCCWARNRPC])
fi
AC_PROG_RANLIB

##
# Checks for header files.
##
AC_HEADER_STDC
AC_CHECK_HEADERS( \
  getopt.h \
  zlib.h \
  lua.h \
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
)
X_AC_CHECK_PTHREADS
X_AC_WRAP
X_AC_CHECK_COND_LIB(z, compress)
X_AC_CHECK_COND_LIB(lua, luaL_newstate)

##
# Check for package feature enablement
##
X_AC_DOTL
X_AC_MUNGE
X_AC_LARGEIO

##
# For list.c, hostlist.c, hash.c
##
AC_DEFINE(WITH_LSD_FATAL_ERROR_FUNC, 1, [Define lsd_fatal_error])
AC_DEFINE(WITH_LSD_NOMEM_ERROR_FUNC, 1, [Define lsd_fatal_error])

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  diod.spec \
  libdiod/Makefile \
  liblsd/Makefile \
  libnpfs/Makefile \
  man/Makefile \
  diod/diod.8 \
  diodctl/diodctl.8 \
  diodmount/diodmount.8 \
  man/diodctl.conf.5 \
  man/9p-attach.5 \
  man/9p-flush.5 \
  man/9p-flock.5 \
  man/9p-lock.5 \
  man/9p-read.5 \
  man/9p-version.5 \
  man/9p-clunk.5 \
  man/9p-intro.5 \
  man/9p-remove.5 \
  man/9p-walk.5 \
  man/9p-error.5 \
  man/9p-open.5 \
  man/9p-rename.5 \
  man/9p-stat.5 \
  man/9p-statfs.5 \
  scripts/Makefile \
  scripts/diodctl.init \
  test/Makefile \
  diod/Makefile \
  diodctl/Makefile \
  diodmount/Makefile \
)

AC_OUTPUT
