##*****************************************************************************
## $Id: ac_curses.m4,v 1.1.1.1 2003/09/05 16:05:42 achu Exp $
##*****************************************************************************
#  AUTHOR:
#    Mark Pulford <mark@kyne.com.au>
#
#  SYNOPSIS:
#    X_AC_CURSES
#
#  DESCRIPTION:
#    Detect SysV compatible curses, such as ncurses.
#
#    Defines HAVE_CURSES_H or HAVE_NCURSES_H if curses is found. 
#    LIBCURSES is also set with the required libary, but is not appended 
#    to LIBS automatically.  If no working curses libary is found 
#    LIBCURSES will be left blank.
#
#    This macro adds the option "--with-ncurses" to configure which can 
#    force the use of ncurses or nothing at all.
#
#*****************************************************************************

AC_DEFUN([X_AC_CURSES],
  [AC_ARG_WITH(ncurses, [  --with-ncurses          Force the use of ncurses over curses],,)
   mp_save_LIBS="$LIBS"
   LIBCURSES=""
   if test "$with_ncurses" != yes
   then
     AC_CACHE_CHECK([for working curses], mp_cv_curses,
       [LIBS="$LIBS -lcurses"
        AC_TRY_LINK(
          [#include <curses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          mp_cv_curses=yes, mp_cv_curses=no)])
     if test "$mp_cv_curses" = yes
     then
       AC_DEFINE([HAVE_CURSES_H],[],[Define if you have curses.h])
       LIBCURSES="-lcurses"
     fi
   fi
   if test ! "$LIBCURSES"
   then
     AC_CACHE_CHECK([for working ncurses], mp_cv_ncurses,
       [LIBS="$mp_save_LIBS -lncurses"
        AC_TRY_LINK(
          [#include <ncurses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          mp_cv_ncurses=yes, mp_cv_ncurses=no)])
     if test "$mp_cv_ncurses" = yes
     then
       AC_DEFINE([HAVE_NCURSES_H],[],[Define if you have ncurses.h])
       LIBCURSES="-lncurses"
     fi
   fi
   LIBS="$mp_save_LIBS"
   AC_SUBST(LIBCURSES)
])dnl
