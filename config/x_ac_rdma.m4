AC_DEFUN([X_AC_RDMA], [

got_rdma=no
AC_ARG_ENABLE([rdma],
  [AS_HELP_STRING([--enable-rdma],
      [build Infiniband RDMA transport (experimental)])],
  [want_rdma=yes], [want_rdma=no])

if test x$want_rdma == xyes; then
  X_AC_CHECK_COND_LIB(rdmacm, rdma_accept)
  X_AC_CHECK_COND_LIB(ibverbs, ibv_alloc_pd)
  AC_CHECK_HEADER([infiniband/verbs.h])
  AC_CHECK_HEADER([rdma/rdma_cma.h])
  if test x$ac_cv_lib_rdmacm_rdma_accept == xyes -a \
      x$ac_cv_lib_ibverbs_ibv_alloc_pd == xyes -a \
      x$ac_cv_header_infiniband_verbs_h == xyes -a \
      x$ac_cv_header_rdma_rdma_cma_h == xyes; then
    got_rdma=yes
    AC_DEFINE([WITH_RDMA], [1], [build Infiniband RDMA transport])
  else
    AC_MSG_ERROR([Could not configure RDMA: missing ibverbs/rdmacm packages])
  fi
fi

AM_CONDITIONAL([RDMA], [test "x$got_rdma" != xno])

])
