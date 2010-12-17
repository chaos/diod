AC_DEFUN([X_AC_LARGEIO], [
  AC_ARG_ENABLE([largeio],
    [AS_HELP_STRING([--enable-largeio], [Build to use large I/O protocol extensions])],
    [enable_largeio=$enableval], [enable_largeio=no])
  if test x$enable_largeio == xyes; then
    AC_DEFINE(HAVE_LARGEIO, 1, [Define if using large I/O protocol extensions])
  fi
  AM_CONDITIONAL([LARGEIO], [test x$enable_largeio == xyes])
])dnl

