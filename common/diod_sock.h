enum {
    DIOD_SOCK_SKIPLISTEN=1,
    DIOD_SOCK_QUIET_EADDRINUSE=2,
};

int  diod_sock_listen_nfds  (struct pollfd **fdsp, int *nfdsp, int nfds,
                             int starting);

int  diod_sock_listen_hostport_list (List l, struct pollfd **fdsp, int *nfdsp,
                                     char *nport, int flags);

void diod_sock_accept_loop (Npsrv *srv, struct pollfd *fds, int nfds, int wrap);

int diod_sock_tryconnect (List l, char *port, int maxtries, int retry_wait_ms);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
