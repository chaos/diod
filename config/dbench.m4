AC_DEFUN([DBENCH], [

enable_dbench=no
X_AC_CHECK_COND_LIB(popt, poptGetArgs)
if test x$ac_cv_lib_popt_poptGetArgs == xyes; then
enable_dbench=yes
# here to 'else # popt' is taken from dbench configure.ac

AC_HEADER_DIRENT
AC_HEADER_TIME
AC_HEADER_SYS_WAIT

AC_CHECK_HEADERS(ctype.h strings.h stdlib.h string.h sys/vfs.h sys/statvfs.h stdint.h)

AC_CHECK_HEADERS(sys/attributes.h attr/xattr.h sys/xattr.h sys/extattr.h sys/uio.h)
AC_CHECK_HEADERS(sys/mount.h)

AC_CHECK_FUNCS(fdatasync)
# Check if we have libattr
AC_SEARCH_LIBS(getxattr, [attr])
AC_SEARCH_LIBS(socket, [socket])
AC_SEARCH_LIBS(gethostbyname, [nsl])

AC_CHECK_FUNCS(getxattr lgetxattr fgetxattr listxattr llistxattr)
AC_CHECK_FUNCS(flistxattr removexattr lremovexattr fremovexattr)
AC_CHECK_FUNCS(setxattr lsetxattr fsetxattr)
# Check if we have attr_get
AC_CHECK_FUNCS(attr_get attr_list attr_set attr_remove)
AC_CHECK_FUNCS(attr_getf attr_listf attr_setf attr_removef)
# Check if we have extattr
AC_CHECK_FUNCS(extattr_delete_fd extattr_delete_file extattr_delete_link)
AC_CHECK_FUNCS(extattr_get_fd extattr_get_file extattr_get_link)
AC_CHECK_FUNCS(extattr_list_fd extattr_list_file extattr_list_link)
AC_CHECK_FUNCS(extattr_set_fd extattr_set_file extattr_set_link)
AC_CHECK_FUNCS(snprintf vsnprintf asprintf vasprintf)

if test x"$ac_cv_func_fgetxattr" = x"yes" -o \
        x"$ac_cv_func_attr_getf" = x"yes" -o \
        x"$ac_cv_func_extattr_get_fd" = x"yes"; then
	    AC_DEFINE(HAVE_EA_SUPPORT, 1, [Whether we have EA support])
fi

AC_CACHE_CHECK([for va_copy],dbench_cv_HAVE_VA_COPY,[
AC_TRY_LINK([#include <stdarg.h>
va_list ap1,ap2;], [va_copy(ap1,ap2);],
dbench_cv_HAVE_VA_COPY=yes,dbench_cv_HAVE_VA_COPY=no)])
if test x"$dbench_cv_HAVE_VA_COPY" = x"yes"; then
    AC_DEFINE(HAVE_VA_COPY,1,[Whether va_copy() is available])
fi

if test x"$dbench_cv_HAVE_VA_COPY" != x"yes"; then
AC_CACHE_CHECK([for __va_copy],dbench_cv_HAVE___VA_COPY,[
AC_TRY_LINK([#include <stdarg.h>
va_list ap1,ap2;], [__va_copy(ap1,ap2);],
dbench_cv_HAVE___VA_COPY=yes,dbench_cv_HAVE___VA_COPY=no)])
if test x"$dbench_cv_HAVE___VA_COPY" = x"yes"; then
    AC_DEFINE(HAVE___VA_COPY,1,[Whether __va_copy() is available])
fi
fi

else # popt
AC_MSG_WARN([no libpopt so 'make check' will skip dbench run])
fi
AM_CONDITIONAL([DBENCH], [test x$enable_dbench == xyes])
])

