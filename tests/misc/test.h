static inline void
_lock (pthread_mutex_t *l)
{
    int n = pthread_mutex_lock (l);
    if (n)
        errn_exit (n, "_lock");
}
static inline void
_unlock (pthread_mutex_t *l)
{
    int n = pthread_mutex_unlock (l);
    if (n)
        errn_exit (n, "_unlock");
}
static inline void
_condsig (pthread_cond_t *c)
{
    int n = pthread_cond_signal (c);
    if (n)
        errn_exit (n, "_condsig");
}
static inline void
_condwait (pthread_cond_t *c, pthread_mutex_t *l)
{
    int n = pthread_cond_wait (c, l);
    if (n)
        errn_exit (n, "_condwait");
}
static inline void
_create (pthread_t *t, void *(f)(void *), void *a)
{
    int n = pthread_create (t, NULL, f, a);
    if (n)
        errn_exit (n,"_create");
}
static inline void
_join (pthread_t t, void **a)
{
    int n = pthread_join (t, a);
    if (n)
        errn_exit (n,"_join");
}
static inline int
_mkstemp (char *p)
{
    int fd = mkstemp (p);
    if (fd < 0)
        err_exit ("_mkstemp");
    return fd;
}
static inline void
_fstat (int fd, struct stat *sb)
{
    if (fstat (fd, sb) < 0)
        err_exit ("_fstat");
}
static inline void
_unlink (char *p)
{
    if (unlink (p) < 0)
        err_exit ("_unlink");
}
static inline void
_fchown (int fd, uid_t u, gid_t g)
{
    if (fchown (fd, u, g) < 0)
        err_exit ("_fchown");
}
static inline void
_fchmod (int fd, mode_t m)
{
    if (fchmod (fd, m) < 0)
        err_exit ("_fchmod");
}
static inline void
_setgroups (size_t s, gid_t *g)
{
#if 0
    if (setgroups (s, g) < 0)
#else
    if (syscall(SYS_setgroups, s, g) < 0)
#endif
        err_exit ("_setgroups");
    
}
static inline int
_getgroups (size_t s, gid_t *g)
{
    int n = getgroups (s, g);
    if (n < 0)
        err_exit ("_getgroups");
    return n;
}
static inline void
_setreuid (uid_t r, uid_t u)
{
    if (setreuid (r, u) < 0)
        err_exit ("_setreuid");
}
static inline void
_setregid (gid_t r, gid_t g)
{
    if (setregid (r, g) < 0)
        err_exit ("_setregid");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

