typedef struct {
    List exports;
    char *port;
} query_t;

query_t *ctl_query (char *host, char *opts, int vopt, int getport,
                    char *payload, char *opt_debug);
void free_query (query_t *q);
