dnl Probe for systemd libraries and installation paths.
dnl
dnl Provides the RRA_WITH_SYSTEMD_UNITDIR macro, which adds the
dnl --with-systemdsystemunitdir configure flag, sets the systemdsystemunitdir
dnl substitution variable, and provides the HAVE_SYSTEMD Automake conditional
dnl to use to control whether to install unit files.
dnl
dnl Provides the RRA_LIB_SYSTEMD_DAEMON_OPTIONAL macro, which sets
dnl SYSTEMD_CFLAGS and SYSTEMD_LIBS substitution variables if
dnl libsystemd-daemon is available and defines HAVE_SD_NOTIFY.  pkg-config
dnl support for libsystemd-daemon is required for it to be detected.
dnl
dnl Depends on the Autoconf macros that come with pkg-config.
dnl
dnl The canonical version of this file is maintained in the rra-c-util
dnl package, available at <http://www.eyrie.org/~eagle/software/rra-c-util/>.
dnl
dnl Written by Russ Allbery <eagle@eyrie.org>
dnl Copyright 2013, 2014
dnl     The Board of Trustees of the Leland Stanford Junior University
dnl
dnl This file is free software; the authors give unlimited permission to copy
dnl and/or distribute it, with or without modifications, as long as this
dnl notice is preserved.

dnl Determine the systemd system unit directory, along with a configure flag
dnl to override, and sets @systemdsystemunitdir@.  Provides the Automake
dnl HAVE_SYSTEMD Automake conditional.
AC_DEFUN([RRA_WITH_SYSTEMD_UNITDIR],
[AC_REQUIRE([PKG_PROG_PKG_CONFIG])
 AS_IF([test x"$PKG_CONFIG" = x], [PKG_CONFIG=false])
 AC_ARG_WITH([systemdsystemunitdir],
    [AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
        [Directory for systemd service files])],
    [],
    [with_systemdsystemunitdir=\${prefix}$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
 AS_IF([test x"$with_systemdsystemunitdir" != xno],
    [AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])
 AM_CONDITIONAL([HAVE_SYSTEMD],
    [test -n "$with_systemdsystemunitdir" -a x"$with_systemdsystemunitdir" != xno])])

dnl Check for libsystemd-daemon and define SYSTEMD_DAEMON_{CFLAGS,LIBS} if it
dnl is available.
AC_DEFUN([RRA_LIB_SYSTEMD_DAEMON_OPTIONAL],
[PKG_CHECK_EXISTS([libsystemd-daemon],
    [PKG_CHECK_MODULES([SYSTEMD_DAEMON], [libsystemd-daemon])
     AC_DEFINE([HAVE_SD_NOTIFY], 1, [Define if sd_notify is available.])])])
