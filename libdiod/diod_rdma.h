typedef struct diod_rdma_struct *diod_rdma_t;

diod_rdma_t diod_rdma_create (void);
int diod_rdma_listen (diod_rdma_t rdma);
void diod_rdma_accept_one (Npsrv *srv, diod_rdma_t rdma);
void diod_rdma_shutdown (diod_rdma_t rdma);
void diod_rdma_destroy (diod_rdma_t rdma);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
