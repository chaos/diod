struct pollfd;

void diod_sock_accept_one (Npsrv *srv, int fd);

void diod_sock_startfd (Npsrv *srv, int fdin, int fdout, char *client_id);

int  diod_sock_listen_hostports (List l, struct pollfd **fdsp, int *nfdsp,
                                     char *nport);

#define DIOD_SOCK_QUIET     0x01

int diod_sock_connect (char *host, char *port, int flags);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
