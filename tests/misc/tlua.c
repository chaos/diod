#if HAVE_CONFIG_H
#include "config.h"
#endif

int
main (int argc, char *argv[])
{
#if HAVE_LUA_H
	return 0;
#else
	return 1;
#endif
}
