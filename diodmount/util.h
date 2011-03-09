int util_mkdir_p (char *path, mode_t mode);
int util_update_mtab (char *dev, char *dir);
void util_mount (const char *source, const char *target, const void *data);
void util_umount (const char *target);
void util_parse_device (char *device, char **anamep, char **hostp);
