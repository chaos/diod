AC_DEFUN([X_AC_DOTL], [
  AC_ARG_ENABLE([dotl],
    [AS_HELP_STRING([--enable-dotl], [Build to use 9P2000.L protocol])],
    [enable_dotl=$enableval], [enable_dotl=no])
  if test x$enable_dotl == xyes; then
    AC_DEFINE(HAVE_DOTL, 1, [Define if using 9P2000.L protocol])
  fi
  AM_CONDITIONAL([DOTL], [test x$enable_dotl == xyes])
])dnl

