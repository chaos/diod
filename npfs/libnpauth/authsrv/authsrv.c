
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  #include "winhelp.c"
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #define __cdecl
#endif

#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "../npaimpl.h"

char *dom = "testdom.com";

struct ident {
    char *user;
    char *passwd;
    char key[7];
};
struct ident users[] = {
    // If your server uses uxusers then these accounts must exist locally
    { "root", "rootpw" },
    { "bin", "binpw" },
    { 0, 0 },
};

static void
xperror(char *msg)
{
    perror(msg);
    exit(1);
}

static struct ident *
findUser(char *user) 
{
    int i;

    for(i = 0; users[i].user; i++) {
        if(strcmp(users[i].user, user) == 0)
            return &users[i];
    }
    return NULL;
}

static void
initUsers()
{
    int i;

    for(i = 0; users[i].user; i++) {
        makeKey(users[i].passwd, users[i].key);
    }
}

static char *
getKey(char *user, char key[7])
{
    struct ident *u;

    u = findUser(user);
    if(u) {
        printf("using user=%s password=%s\n", user, u->passwd);
        return u->key;
    }
    printf("user %s not found, sending garbage\n", user);
    getRand(key, 7);
    return key;
}

static int
speaksFor(char *idc, char *idr)
{
    return strcmp(idc, idr) == 0;
}

static void
serv1(int s)
{
    char outbuf[145], treqbuf[141], kn[7], kc[7], ks[7];
    struct ticketreq treq;
    struct ticket ctick, stick;

    if(read(s, treqbuf, sizeof treqbuf) != sizeof treqbuf
    || decTicketReq(treqbuf, &treq) == -1
    || treq.type != AuthTreq
    || strcmp(treq.dom, dom) != 0
    || !speaksFor(treq.idc, treq.idr))
        goto err;

    getRand(kn, sizeof kn);

    ctick.type = AuthTc;
    ctick.ch = treq.ch;
    ctick.idc = treq.idc;
    ctick.idr = treq.idr;
    ctick.key = kn;

    stick.type = AuthTs;
    stick.ch = treq.ch;
    stick.idc = treq.idc;
    stick.idr = treq.idr;
    stick.key = kn;

    outbuf[0] = AuthOK;
    if(encTicket(outbuf+1, &ctick, getKey(treq.idc, kc)) == -1
    || encTicket(outbuf+73, &stick, getKey(treq.ids, ks)) == -1
    || write(s, outbuf, sizeof outbuf) != sizeof outbuf)
        goto err;
    close(s);
    return;

err:
    printf("error\n");
    close(s);
}

static int
tcp_listen(int port)
{
    struct sockaddr_in addr;
    int s;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == -1)
        xperror("socket");
    if(bind(s, (struct sockaddr *)&addr, sizeof addr) == -1)
        xperror("bind");
    if(listen(s, 5) == -1)
        xperror("listen");
    return s;
}

void
server(int port) 
{
    struct sockaddr_in addr;
    size_t adlen;
    int s, s2;

    s = tcp_listen(port);
    for(;;) {
        adlen = sizeof addr;
        s2 = accept(s, (struct sockaddr *)&addr, &adlen);
        if(s2 == -1) 
            xperror("accept");
        printf("connection from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
#ifdef _WIN32
        // just one at a time for now...
        serv1(s2);
#else
        switch(fork()) {
        case -1:
            perror("fork");
            break;
        case 0:
            close(s);
            serv1(s2);
            exit(0);
            break;
        default:
            break;
        }
#endif
        close(s2);
    }
}

void
usage(char *prog)
{
    fprintf(stderr, "usage: %s [-p port]", prog);
    exit(1);
}

int __cdecl
main(int argc, char **argv)
{
    int port, ch;
    char *prog;

#ifdef _WIN32
    init();
#endif
    prog = argv[0];
    port = 567;
    while((ch = getopt(argc, argv, "p:")) != -1) {
        switch(ch) {
        case 'p':
            port = atoi(optarg);
            break;
        default:
            usage(prog);
        }
    }
    argc -= optind;
    argv += optind;
    if(argc)
        usage(prog);

    initUsers();
    server(port);
    return 0;
}

