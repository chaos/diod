void diod_log_init (char *p);
void diod_log_fini (void);
void diod_log_set_dest (char *dest);
char *diod_log_get_dest (void);
void diod_log_msg (const char *fmt, va_list ap);

void err_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void err (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
void errn_exit (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3), noreturn));
void errn (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
void msg_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void msg (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
