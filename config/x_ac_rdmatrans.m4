AC_DEFUN([X_AC_RDMATRANS], [

got_rdmatrans=no
AC_ARG_ENABLE([rdmatrans],
  [AS_HELP_STRING([--enable-rdmatrans], [build Infiniband RDMA transport])],
  [want_rdmatrans=yes], [want_rdmatrans=no])

if test x$want_rdmatrans == xyes; then
  X_AC_CHECK_COND_LIB(rdmacm, rdma_accept)
  X_AC_CHECK_COND_LIB(ibverbs, ibv_alloc_pd)
  AC_CHECK_HEADER([infiniband/verbs.h])
  AC_CHECK_HEADER([rdma/rdma_cma.h])
  if test x$ac_cv_lib_rdmacm_rdma_accept == xyes -a \
      x$ac_cv_lib_ibverbs_ibv_alloc_pd == xyes -a \
      x$ac_cv_header_infiniband_verbs_h == xyes -a \
      x$ac_cv_header_rdma_rdma_cma_h == xyes; then
    got_rdmatrans=yes
    AC_DEFINE([WITH_RDMATRANS], [1], [build Infiniband RDMA transport])
  else
    AC_MSG_WARN([omitting support for infiniband RDMA transport])
  fi
fi

AM_CONDITIONAL([RDMATRANS], [test "x$got_rdmatrans" != xno])

])
