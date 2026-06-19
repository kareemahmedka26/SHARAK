/* Expose POSIX (usleep, INADDR_LOOPBACK, sockets) under strict -std=c17. */
#define _DEFAULT_SOURCE

/*
 * fleet_main.c — TCP server front-end for the Sharak fleet node simulator.
 *
 * Mirrors the real node under QEMU (scripts/run_qemu.sh exposes UART0 as
 * `-serial tcp:127.0.0.1:PORT,server,nowait`): this opens a TCP server on a
 * port, and once a client connects it streams byte-exact telemetry frames at a
 * fixed rate. Run several instances on different ports to present a multi-node
 * load to the gateway (REQ-SIM-002/003); distinct ports = distinct identities.
 *
 *   Usage: fleet_node [--port N] [--rate HZ]
 *   Read it like the real node:  nc 127.0.0.1 5555 | xxd | head
 */

#include "fleet_node.h"
#include <sharak/protocol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static int make_server(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int yes = 1;                                  /* allow quick re-bind after restart */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 only */
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 1) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    uint16_t port = 5555;
    long     rate = 100;                          /* frames per second */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
            rate = strtol(argv[++i], NULL, 10);
            if (rate < 1) rate = 1;
        } else {
            fprintf(stderr, "usage: %s [--port N] [--rate HZ]\n", argv[0]);
            return 2;
        }
    }

    signal(SIGPIPE, SIG_IGN);                     /* don't die if client drops mid-send */

    int srv = make_server(port);
    if (srv < 0) return 1;
    fprintf(stderr, "fleet_node: serving on 127.0.0.1:%u at %ld Hz\n", port, rate);

    const long period_us = 1000000L / rate;
    uint32_t phase = 0;

    for (;;) {                                    /* accept clients forever */
        int cli = accept(srv, NULL, NULL);
        if (cli < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        fprintf(stderr, "fleet_node: client connected\n");

        for (;;) {                                /* stream until the client leaves */
            uint16_t seq; int32_t x, y, z;
            fleet_sample(phase++, &seq, &x, &y, &z);

            uint8_t frame[SK_FRAME_MAX];
            int n = fleet_build_frame(seq, x, y, z, frame, sizeof frame);
            if (n < 0) { fprintf(stderr, "encode error %d\n", n); break; }

            ssize_t off = 0;                      /* write the whole frame, handle partials */
            while (off < n) {
                ssize_t w = write(cli, frame + off, (size_t)(n - off));
                if (w <= 0) { off = -1; break; }  /* client gone */
                off += w;
            }
            if (off < 0) break;
            usleep((useconds_t)period_us);
        }
        close(cli);                               /* no fd leak: close before re-accept */
        fprintf(stderr, "fleet_node: client disconnected\n");
    }
    close(srv);
    return 0;
}
