struct pollfd;

void diod_sock_accept_one (Npsrv *srv, int fd);

void diod_sock_startfd (Npsrv *srv, int fdin, int fdout, char *client_id,
                        int flags);

int  diod_sock_listen (List l, struct pollfd **fdsp, int *nfdsp);

#define DIOD_SOCK_QUIET     0x01
#define DIOD_SOCK_PRIVPORT  0x02 

int diod_sock_connect (char *name, int flags);
int diod_sock_connect_inet (char *host, char *port, int flags);
int diod_sock_connect_unix (char *path, int flags);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
