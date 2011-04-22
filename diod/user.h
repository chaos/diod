#define SQUASH_UNAME    "nobody"

int diod_switch_user (Npuser *u, gid_t gid_override);
void diod_become_user (char *name, uid_t uid, int realtoo);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
