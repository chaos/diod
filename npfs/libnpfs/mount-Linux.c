#include <sys/mount.h>
#include <sys/syscall.h>

int np_mount(char *mntpt, int mntflags, char *opts)
{
	return mount("none", mntpt, "9p", mntflags, opts);
}

int
sreuid(int a, int b)
{
	return syscall(SYS_setreuid, a, b);
}

int
sregid(int a, int b)
{
	return syscall(SYS_setregid, a, b);
}
