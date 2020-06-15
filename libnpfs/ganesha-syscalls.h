/* This file is in the public domain */
#ifndef ganesha_syscalls_h_inc
#define ganesha_syscalls_h_inc

#include <sys/types.h>

extern int init_ganesha_syscalls(void);
extern int fbsd_setthreaduid(uid_t);
extern int fbsd_setthreadgid(gid_t);
extern int fbsd_setthreadgroups(unsigned int, gid_t *);

#endif
