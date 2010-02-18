int  diod_sock_listen      (struct pollfd *fds, int nfds);
int  diod_sock_listen_fds  (struct pollfd **fdsp, int *nfdsp, int nfds);
int  diod_sock_listen_list (struct pollfd **fdsp, int *nfdsp, List l);
void diod_sock_accept_loop (Npsrv *srv, struct pollfd *fds, int nfds, int wrap);
int  diod_sock_setup_one   (char *host, char *port, struct pollfd **fdsp,
                            int *nfdsp);
int  diod_sock_setup_alloc (char *host, struct pollfd **fdsp, int *nfdsp,
                            char **kp);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
