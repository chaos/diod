AC_DEFUN([X_AC_TCMALLOC], [

got_tcmalloc=no
AC_ARG_WITH([tcmalloc],
  [AS_HELP_STRING([--with-tcmalloc], [build with Google-perftools malloc])],
  [want_tcmalloc=yes], [want_tcmalloc=no])

if test x$want_tcmalloc == xyes; then
  X_AC_CHECK_COND_LIB(tcmalloc, tc_cfree)
  if test x$ac_cv_lib_tcmalloc_tc_cfree == xyes; then
    got_tcmalloc=yes
    LIBTCMALLOC="-ltcmalloc"
    AC_DEFINE([WITH_TCMALLOC], [1], [build with Google-perftools malloc])
  else
    AC_MSG_ERROR([building without Google-perftools malloc])
  fi
fi

AM_CONDITIONAL([TCMALLOC], [test "x$got_tcmalloc" != xno])

])
