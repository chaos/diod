struct pollfd;

void diod_sock_accept_one (Npsrv *srv, int fd);

void diod_sock_startfd (Npsrv *srv, int fd, char *client_id);

int  diod_sock_listen_hostports (List l, struct pollfd **fdsp, int *nfdsp,
                                     char *nport);

int diod_sock_connect (char *host, char *port, int maxtries, int retry_wait_ms);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
