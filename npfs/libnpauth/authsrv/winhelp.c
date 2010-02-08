#include <windows.h>
#include <winsock2.h>
#include <string.h>

static int optind = 1;
static int optpos = 0;
static char *optarg = NULL;

static int
getopt(int argc, char **argv, const char *opts)
{
	char *p, ch;

	if(optind >= argc || !argv[optind])
		return -1;
	if(optpos && !argv[optind][optpos]) {
		optind ++;
		optpos = 0;
	}
	if(optind >= argc || !argv[optind])
		return -1;
	if(optpos == 0 && argv[optind][optpos++] != '-')
		return -1;
	ch = argv[optind][optpos++];
	p = strchr(opts, ch);
	if(!p)
		return '?';
	if(p[1] != ':')
		return ch;

	optarg = argv[optind++] + optpos;
	optpos = 0;
	if(*optarg)
		return ch;
	if(optind >= argc || !argv[optind])
		return '?';
	optarg = argv[optind++];
	return ch;
}

static void
init() {
	WSADATA wsData;

	WSAStartup(MAKEWORD(2,2), &wsData);
}

