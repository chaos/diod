/* tlua.c - is config file support configured? */

#if HAVE_CONFIG_H
#include "config.h"
#endif

int
main (int argc, char *argv[])
{
#if HAVE_CONFIG_FILE
	return 0;
#else
	return 1;
#endif
}
