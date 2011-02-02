#define IXP_NO_P9_
#include <ixp.h>
#include <stdbool.h>

#undef ulong
#define ulong _ixpulong
typedef unsigned long ulong;

#ifdef CPROTO
# undef bool
typedef int bool;
typedef char* va_list;
#endif

char *argv0;
#define ARGBEGIN \
		int _argtmp=0, _inargv=0; char *_argv=nil; \
		if(!argv0) {argv0=*argv; argv++, argc--;} \
		_inargv=1; USED(_inargv); \
		while(argc && argv[0][0] == '-') { \
			_argv=&argv[0][1]; argv++; argc--; \
			if(_argv[0] == '-' && _argv[1] == '\0') \
				break; \
			while(*_argv) switch(*_argv++)
#define ARGEND }_inargv=0;USED(_argtmp, _argv, _inargv)

#define EARGF(f) ((_inargv && *_argv) ? \
			(_argtmp=strlen(_argv), _argv+=_argtmp, _argv-_argtmp) \
			: ((argc > 0) ? \
				(--argc, ++argv, _used(argc), *(argv-1)) \
				: ((f), (char*)0)))
#define ARGF() EARGF(_used(0))

#ifndef KENC
  static inline void _used(long a, ...) { if(a){} }
# define USED(...) _used((long)__VA_ARGS__)
# define SET(x) (x = 0)
/* # define SET(x) USED(&x) GCC 4 is 'too smart' for this. */
#endif

#undef nil
#define nil ((void*)0)
#define nelem(ary) (sizeof(ary) / sizeof(*ary))

#define thread ixp_thread

#define eprint ixp_eprint
#define emalloc ixp_emalloc
#define emallocz ixp_emallocz
#define estrdup ixp_estrdup
#define erealloc ixp_erealloc
#define strlcat ixp_strlcat
#define tokenize ixp_tokenize

#define muxinit ixp_muxinit
#define muxfree ixp_muxfree
#define muxrpc ixp_muxrpc

#define errstr ixp_errstr
#define rerrstr ixp_rerrstr
#define werrstr ixp_werrstr

typedef struct IxpMap Map;
typedef struct MapEnt MapEnt;

typedef IxpTimer Timer;

typedef struct timeval timeval;

struct IxpMap {
	MapEnt**	bucket;
	int		nhash;

	IxpRWLock	lock;
};

struct IxpTimer {
	Timer*		link;
	uint32_t	msec;
	long		id;
	void		(*fn)(long, void*);
	void*		aux;
};

/* map.c */
void	ixp_mapfree(IxpMap*, void(*)(void*));
void	ixp_mapexec(IxpMap*, void(*)(void*, void*), void*);
void	ixp_mapinit(IxpMap*, MapEnt**, int);
bool	ixp_mapinsert(IxpMap*, ulong, void*, bool);
void*	ixp_mapget(IxpMap*, ulong);
void*	ixp_maprm(IxpMap*, ulong);

/* mux.c */
void	muxfree(IxpClient*);
void	muxinit(IxpClient*);
IxpFcall*	muxrpc(IxpClient*, IxpFcall*);

/* timer.c */
long	ixp_nexttimer(IxpServer*);

