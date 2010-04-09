void diodctl_serv_init (void);
int  diodctl_serv_getname (Npuser *user, char *jobid, u64 offset, u32 count,
                           u8* data);
int  diodctl_serv_create (Npuser *user, char *jobid);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
