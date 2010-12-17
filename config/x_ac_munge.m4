AC_DEFUN([X_AC_MUNGE], [
  AC_ARG_ENABLE([munge],
    [AS_HELP_STRING([--enable-munge], [Build to use MUNGE authentication])],
    [enable_munge=$enableval], [enable_munge=no])
  if test x$enable_munge == xyes; then
    AC_CHECK_HEADERS([munge.h])
    X_AC_CHECK_COND_LIB(munge, munge_ctx_create)
    if test x$ac_cv_header_munge_h != xyes; then
      AC_MSG_ERROR([Please install munge or configure --disable-munge])
    fi
    AC_DEFINE(HAVE_MUNGE, 1, [Define if using MUNGE authentication])
  fi
  AM_CONDITIONAL([MUNGE], [test x$enable_munge == xyes])
])dnl

