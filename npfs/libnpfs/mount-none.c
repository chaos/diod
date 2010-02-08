#include <unistd.h>

int np_mount(char *mntpt, int mntflags, char *opts)
{
	return -1;
}

int
sreuid(int a, int b)
{
	return setreuid(a, b);
}

int
sregid(int a, int b)
{
	return setregid(a, b);
}
