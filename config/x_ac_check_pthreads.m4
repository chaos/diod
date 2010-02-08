##*****************************************************************************
## $Id: x_ac_check_pthreads.m4 391 2005-02-10 02:31:11Z dun $
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CHECK_PTHREADS
#
#  DESCRIPTION:
#    Check to see how to link a process against Pthreads.
#
#    Also define both _REENTRANT and _THREAD_SAFE which may be needed when
#    linking against multithreaded code.  By defining them here, the define
#    goes into "config.h" which is the first include (in my code, at least).
#    For more information wrt _REENTRANT, refer to the LinuxThreads FAQ:
#      <http://pauillac.inria.fr/~xleroy/linuxthreads/faq.html#H>.
##*****************************************************************************

AC_DEFUN([X_AC_CHECK_PTHREADS], [
  AC_CACHE_CHECK(
    [how to link against pthreads],
    [x_ac_cv_check_pthreads], [
      LIBPTHREAD=""
      _x_ac_check_pthreads_libs_save="$LIBS"
      for flag in -lpthread -pthread; do
        LIBS="$flag"
        AC_LINK_IFELSE([
          AC_LANG_PROGRAM(
            [[#include <pthread.h>]],
            [[pthread_join (0, 0);]]
          )],
          [x_ac_cv_check_pthreads="$flag"; break],
          [x_ac_cv_check_pthreads=FAILED]
        )
      done
      LIBS="$_x_ac_check_pthreads_libs_save" ]
  )
  if test "$x_ac_cv_check_pthreads" = "FAILED"; then
    AC_MSG_FAILURE([cannot link against pthreads])
  fi
  LIBPTHREAD="$x_ac_cv_check_pthreads"
  AC_SUBST(LIBPTHREAD)
  AC_DEFINE([_REENTRANT], [1],
    [Define to 1 if you plan to link against multithreaded code.]
  )
  AC_DEFINE([_THREAD_SAFE], [1],
    [Define to 1 if you plan to link against multithreaded code.]
  )]
)
