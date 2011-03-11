typedef struct {
    List exports;
    char *port;
} query_t;

query_t *ctl_query (char *host, char *jobid);
void free_query (query_t *q);
