
#ifdef _WIN32
  // all OpenSSL functions are CDECL, although none of the headers
  // have annotations for this.  As a quick workaround, we use CPP
  // to add annotations.  
  // We can't simply force all functions to be CDECL because we want
  // to be able to link against other libraries that may not be CDECL
  #define DECL __cdecl
  #define DES_set_key_checked DECL DES_set_key_checked
  #define DES_set_odd_parity  DECL DES_set_odd_parity
  #define DES_ecb_encrypt     DECL DES_ecb_encrypt
  #define RAND_bytes          DECL RAND_bytes
#endif

#include <openssl/rand.h>
#include <openssl/des.h>

#ifdef _WIN32
  #undef DECL
  #define DECL
#endif
