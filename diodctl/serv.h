void diodctl_serv_init (void);
void diodctl_serv_fini (void);
void diodctl_serv_reload (void);

int diodctl_serv_readctl (Npuser *user, char *opts,
                          u64 offset, u32 count, u8* data);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
