/* Copyright (c) 2013, Michael Santos <michael.santos@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "erlxc.h"

#define ERLXC_MSG_SYNC  0
#define ERLXC_MSG_ASYNC (htons(1))

static void erlxc_loop(erlxc_state_t *);
static erlxc_msg_t *erlxc_msg(erlxc_state_t *);
static void usage(erlxc_state_t *);

static int erlxc_write(u_int16_t, ETERM *);
static ssize_t erlxc_read(void *, ssize_t);

extern char *__progname;

    int
main(int argc, char *argv[])
{
    erlxc_state_t *ep = NULL;
    char *name = NULL;
    char *path = NULL;
    char *errlog = NULL;
    int ch = 0;

    erl_init(NULL, 0);

    ep = calloc(1, sizeof(erlxc_state_t));
    if (!ep)
        return -1;

    while ( (ch = getopt(argc, argv, "e:n:p:vxX")) != -1) {
        switch (ch) {
            case 'e':
                errlog = strdup(optarg);
                if (!errlog)
                    erl_err_sys("errlog");
                break;
            case 'n':
                name = strdup(optarg);
                if (!name)
                    erl_err_sys("name");
                break;
            case 'p':
                path = strdup(optarg);
                if (!path)
                    erl_err_sys("path");
                break;
            case 'v':
                ep->verbose++;
                break;
            case 'x':
                ep->opt |= erlxc_opt_destroy_on_exit;
                break;
            case 'X':
                ep->opt &= ~erlxc_opt_destroy_on_exit;
                break;
            default:
                usage(ep);
        }
    }

    if (!name)
        usage(ep);

    if (errlog) {
        if (!freopen(errlog, "w+", stderr))
            erl_err_sys("freopen");
    }

    ep->c = lxc_container_new(name, path);
    if (!ep->c)
        erl_err_quit("failed to create container");

    free(name);
    free(path);

    erlxc_loop(ep);

    if (ep->opt & erlxc_opt_destroy_on_exit) {
        VERBOSE(1, "destroying container:%s", ep->c->name);
        (void)ep->c->stop(ep->c);
        (void)ep->c->destroy(ep->c);
    }

    (void)lxc_container_put(ep->c);
    exit(0);
}

    static void
erlxc_loop(erlxc_state_t *ep)
{
    erlxc_msg_t *msg = NULL;
    ETERM *arg = NULL;
    ETERM *reply = NULL;

    for ( ; ; ) {
        msg = erlxc_msg(ep);
        if (!msg)
            break;

        arg = erl_decode(msg->arg);
        if (!arg)
            erl_err_quit("invalid message");

        reply = erlxc_cmd(ep, msg->cmd, arg);
        if (!reply)
            erl_err_quit("unrecoverable error");

        free(msg->arg);
        free(msg);
        erl_free_compound(arg);

        if (erlxc_write(ERLXC_MSG_SYNC, reply) < 0)
            erl_err_sys("erlxc_write");

        /* Check for defunct processes */
        while (waitpid(-1, 0, WNOHANG) > 0);

        (void)fflush(stderr);
    }
}

    static erlxc_msg_t *
erlxc_msg(erlxc_state_t *ep)
{
    ssize_t n = 0;
    u_int16_t buf = 0;
    u_int16_t len = 0;
    erlxc_msg_t *msg = NULL;

    errno = 0;
    n = erlxc_read(&buf, sizeof(buf));
    
    if (n != sizeof(buf)) {
        if (errno == 0)
            return NULL;

        erl_err_sys("erlxc_msg: expected=%lu, got=%lu", sizeof(buf), n);
    }
    
    len = ntohs(buf);
    
    VERBOSE(2, "erlxc_msg: packet len = %u", len);
    
    if (len >= UINT16_MAX || len < sizeof(buf))
        erl_err_quit("erlxc_msg: invalid len=%d (max=%d)", len, UINT16_MAX);

    len -= sizeof(buf);

    msg = erlxc_malloc(sizeof(erlxc_msg_t));
    msg->arg = erlxc_malloc(len);

    n = erlxc_read(&buf, sizeof(buf));
    if (n != sizeof(buf))
        erl_err_sys("erlxc_msg: expected=%lu, got=%lu", sizeof(buf), n);

    msg->cmd = ntohs(buf);

    n = erlxc_read(msg->arg, len);
    if (n != len)
        erl_err_sys("erlxc_msg: expected=%u, got=%lu", len, n);
    
    return msg;
}

    int
erlxc_send(ETERM *t)
{
    return erlxc_write(ERLXC_MSG_ASYNC, t);
}

    static int
erlxc_write(u_int16_t type, ETERM *t)
{
    int tlen = 0;
    u_int16_t hlen = 0;
    unsigned char *buf = NULL;

    tlen = erl_term_len(t);
    if (tlen < 0 || tlen+sizeof(type) >= UINT16_MAX)
        goto ERR;

    hlen = ntohs(tlen+sizeof(type));

    buf = erlxc_malloc(tlen);

    if (erl_encode(t, buf) < 1)
        goto ERR;

    flockfile(stdout);

    if ( (write(STDOUT_FILENO, &hlen, 2) != 2) ||
         (write(STDOUT_FILENO, &type, 2) != 2) ||
         (write(STDOUT_FILENO, buf, tlen) != tlen))
        goto ERR;

    funlockfile(stdout);

    erl_free(buf);
    return 0;

ERR:
    erl_free(buf);
    return -1;
}

    static ssize_t
erlxc_read(void *buf, ssize_t len)
{
    ssize_t i = 0;
    ssize_t got = 0;

    do {
        if ((i = read(STDIN_FILENO, buf + got, len - got)) <= 0)
            return(i);
        got += i;
    } while (got < len);

    return len;
}

    static void
usage(erlxc_state_t *ep)
{
    (void)fprintf(stderr, "%s %s (lxc %s)\n",
            __progname, ERLXC_VERSION, lxc_get_version());
    (void)fprintf(stderr,
            "usage: %s -n <name> <options>\n"
            "    -n               container name\n"
            "    -e               error log\n"
            "    -p               LXC path\n"
            "    -v               verbose mode\n"
            "    -x               destroy container on exit\n"
            "    -X               do not destroy container on exit\n",
            __progname
            );

    exit (EXIT_FAILURE);
}
