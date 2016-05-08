#ifndef INC_DIOD_DIRENT
#define INC_DIOD_DIRENT

#include <dirent.h>

struct diod_dirent
{
  struct dirent dir_entry;
  /* for when ! _DIRENT_HAVE_D_OFF */
  off_t d_off;
};
#endif
