/* tlua.c - is lua configured? */

#if HAVE_CONFIG_H
#include "config.h"
#endif

int
main (int argc, char *argv[])
{
#if defined(HAVE_LUA_H) && defined(HAVE_LUALIB_H)
	return 0;
#else
	return 1;
#endif
}
