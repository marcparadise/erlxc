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

static void erlxc_loop(erlxc_state_t *);
static erlxc_msg_t *erlxc_msg(erlxc_state_t *);
static void usage(erlxc_state_t *);

static int erlxc_write(ETERM *t);
static ssize_t erlxc_read(void *, ssize_t);

extern char *__progname;

    int
main(int argc, char *argv[])
{
    erlxc_state_t *ep = NULL;
    int ch = 0;

    erl_init(NULL, 0);

    ep = calloc(1, sizeof(erlxc_state_t));
    if (!ep)
        return -1;

    ep->c = calloc(LXCMAX, sizeof(struct lxc_container **));
    if (!ep->c)
        return -1;

    ep->max = LXCMAX;

    while ( (ch = getopt(argc, argv, "v")) != -1) {
        switch (ch) {
            case 'v':
                ep->verbose++;
                break;
            default:
                usage(ep);
        }
    }

    erlxc_loop(ep);
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
        // erl_free_term(arg)
        erl_free_compound(arg);

        if (erlxc_write(reply) < 0)
            erl_err_sys("erlxc_write");

        erl_free_compound(arg);
    }
}

    static erlxc_msg_t *
erlxc_msg(erlxc_state_t *ep)
{
    ssize_t n = 0;
    u_int32_t buf = 0;
    u_int32_t len = 0;
    erlxc_msg_t *msg = NULL;

    errno = 0;
    n = erlxc_read(&buf, sizeof(buf));
    
    if (n != sizeof(buf)) {
        if (errno == 0)
            return NULL;

        erl_err_sys("erlxc_msg: expected=%lu, got=%lu", sizeof(buf), n);
    }
    
    len = ntohl(buf);
    
    VERBOSE(2, "erlxc_msg: packet len = %u", len);
    
    if (len >= MAXBUFLEN || len < 4)
        erl_err_quit("erlxc_msg: invalid len=%d (max=%d)", len, MAXBUFLEN);

    len -= 4;

    msg = erl_malloc(sizeof(erlxc_msg_t));
    if (!msg)
        erl_err_sys("erl_malloc");

    msg->arg = erl_malloc(len);
    if (!msg->arg)
        erl_err_sys("erl_malloc");

    n = erlxc_read(&buf, sizeof(buf));
    if (n != sizeof(buf))
        erl_err_sys("erlxc_msg: expected=%lu, got=%lu", sizeof(buf), n);

    msg->cmd = ntohl(buf);

    n = erlxc_read(msg->arg, len);
    if (n != len)
        erl_err_sys("erlxc_msg: expected=%u, got=%lu", len, n);
    
    return msg;
}

    static int
erlxc_write(ETERM *t)
{
    int tlen = 0;
    int hlen = 0;
    unsigned char *buf = NULL;

    /* XXX overflow */
    tlen = erl_term_len(t);
    hlen = ntohl(tlen);

    buf = erl_malloc(tlen);
    if (!buf)
        return -1;

    if (erl_encode(t, buf) < 1)
        goto ERR;

    if (write(STDOUT_FILENO, &hlen, 4) != 4)
        goto ERR;

    if (write(STDOUT_FILENO, buf, tlen) != tlen)
        goto ERR;

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
    (void)fprintf(stderr, "%s, %s\n", __progname, ERLXC_VERSION);
    (void)fprintf(stderr,
            "usage: %s <options>\n"
            "              -n <name>        LXC name [default:" LXCNAME "]\n"
            "              -p <directory>   LXC path [default:" LXCPATH "]\n"
            "              -v               verbose mode\n",
            __progname
            );

    exit (EXIT_FAILURE);
}
