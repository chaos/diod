##*****************************************************************************
## $Id: x_ac_check_cond_lib.m4 391 2005-02-10 02:31:11Z dun $
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CHECK_COND_LIB(library, function)
#
#  DESCRIPTION:
#    Check whether a program can be linked with <library> to get <function>.
#    Like AC_CHECK_LIB(), except that if the check succeeds, HAVE_LIB<library>
#    will be defined and a shell variable LIB<library> containing "-l<library>"
#    will be substituted via AC_SUBST().
#
#    In other words, this is just like the default action of AC_CHECK_LIB(),
#    except that instead of modifying LIBS (which will affect the linking of
#    all executables), the shell variable LIB<library> is defined so it can be
#    added to the linking of just those executables needing this library.
#    Also note that this checks to see if the library is even needed at all.
##*****************************************************************************

AC_DEFUN([X_AC_CHECK_COND_LIB], [
  AC_CACHE_CHECK(
    [for $2 in default libs],
    [x_ac_cv_lib_none_$2], [
    AC_LINK_IFELSE(
      AC_LANG_CALL([], [$2]),
      AS_VAR_SET(x_ac_cv_lib_none_$2, yes),
      AS_VAR_SET(x_ac_cv_lib_none_$2, no)
    )]
  )
  AS_IF([test AS_VAR_GET(x_ac_cv_lib_none_$2) = no],
    AC_CHECK_LIB(
      [$1],
      [$2], [
        AH_CHECK_LIB([$1])
        AS_TR_CPP([LIB$1])="-l$1";
        AC_SUBST(AS_TR_CPP([LIB$1]))
        AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_LIB$1]))
      ]
    )
  )]
)
