/*
 * bal.c
 *
 * Author:    Ryan M. Lederman <lederman@gmail.com>
 * Copyright: Copyright (c) 2004-2023
 * Version:   0.2.0
 * License:   The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "bal.h"

/*
  Exported functions
 */

int bal_initialize(void)
{
    int r = BAL_FALSE;

#if defined(_WIN32)
    {
        WORD wVer  = MAKEWORD(WSOCK_MINVER, WSOCK_MAJVER);
        WSADATA wd = {0};

        if (0 == WSAStartup(wVer, &wd))
            r = BAL_TRUE;
    }
#else
    r                  = BAL_TRUE;
#endif /* !_WIN32 */

    return r;
}

int bal_finalize(void)
{
    int r = BAL_FALSE;

#if defined(_WIN32)
    {
        if (0 == WSACleanup())
            r = bal_asyncselect(NULL, NULL, BAL_S_DIE);
    }
#else
    r                  = bal_asyncselect(NULL, NULL, BAL_S_DIE);
#endif

    return r;
}

int bal_asyncselect(const balst* s, bal_async_callback proc, uint32_t mask)
{
    int r                          = BAL_FALSE;
    static bal_selectdata_list l   = {0};
    static bal_eventthread_data td = {0};
    static bal_thread t            = NULL;
#if !defined(_WIN32)
    static bal_mutex m = PTHREAD_MUTEX_INITIALIZER;
#else
    static bal_mutex m = NULL;
#endif

    if (BAL_S_DIE == mask) {
        td.die = 1;

        if (BAL_TRUE == _bal_mutex_free(&m)) {
            if (BAL_TRUE == _bal_sdl_clr(&l)) {
                t = NULL;
                memset(&m, 0, sizeof(bal_mutex));
                r = BAL_TRUE;
            }
        }
    }

    if ((s) && (proc)) {
        if (!t) {
            td.die = 0;
            td.l   = &l;
            td.m   = &m;

            if (BAL_FALSE == _bal_initasyncselect(&t, &m, &td))
                return r;
        }

        if (BAL_TRUE == _bal_mutex_lock(&m)) {
            if (0 == mask) {
                if (BAL_TRUE == _bal_sdl_rem(&l, s->sd))
                    r = BAL_TRUE;

            } else

                if (_bal_sdl_size(&l) < (FD_SETSIZE - 1)) {
                bal_selectdata* d = _bal_sdl_find(&l, s->sd);

                if (d) {
                    d->mask = mask;
                    d->proc = proc;
                    r       = BAL_TRUE;

                } else {
                    bal_selectdata d;

                    if (BAL_TRUE == bal_setiomode(s, 1)) {
                        d.mask = mask;
                        d.proc = proc;
                        d.s    = (balst*)s;
                        d._n   = NULL;
                        d._p   = NULL;

                        r = _bal_sdl_add(&l, &d);
                    }
                }
            }

            if (BAL_TRUE == r)
                r = _bal_mutex_unlock(&m);
            else
                _bal_mutex_unlock(&m);
        }
    }

    return r;
}

int bal_autosocket(balst* s, int af, int pt, cbstr host, cbstr port)
{
    int r = BAL_FALSE;

    if ((s) && _bal_validstr(host)) {
        int _af = ((af == 0) ? PF_UNSPEC : af);
        int _st = ((pt == 0) ? 0 : (pt == IPPROTO_TCP) ? SOCK_STREAM : SOCK_DGRAM);
        bal_addrinfo ai;

        memset(&ai, 0, sizeof(ai));

        if (BAL_TRUE == _bal_getaddrinfo(0, _af, _st, host, port, &ai)) {
            const struct addrinfo* a = NULL;

            while (NULL != (a = _bal_enumaddrinfo(&ai))) {
                if (BAL_TRUE == bal_sock_create(s, a->ai_family, a->ai_protocol, a->ai_socktype)) {
                    r = BAL_TRUE;
                    break;
                }
            }

            freeaddrinfo(ai._ai);
        }
    }

    return r;
}

int bal_sock_create(balst* s, int af, int pt, int st)
{
    int r = BAL_FALSE;

    if (-1L != (s->sd = socket(af, st, pt))) {
        s->af = af;
        s->pf = pt;
        s->st = st;
        r     = BAL_TRUE;
    }

    return r;
}

int bal_reset(balst* s)
{
    int r = BAL_FALSE;

    if (s) {
        s->af = -1;
        s->pf = -1;
        s->sd = ((bal_socket)-1);
        s->st = -1;
        s->ud = ((unsigned long)-1);
        r     = BAL_TRUE;
    }

    return r;
}

int bal_close(balst* s)
{
    int r = BAL_FALSE;

    if (s) {
#if defined(_WIN32)
        if (0 == closesocket(s->sd))
            r = bal_reset(s);
#else
        if (0 == close(s->sd))
            r = bal_reset(s);
#endif
    }

    s->_f &= ~BAL_F_PENDCONN;

    return r;
}

int bal_shutdown(balst* s, int how)
{
    if ((NULL != s)) {
        s->_f &= ~BAL_F_PENDCONN;
        return shutdown(s->sd, how);

    } else {
#if defined(_WIN32)
        return SOCKET_ERROR;
#else
        return ENOTSOCK;
#endif // !_WIN32
    }
}

int bal_connect(const balst* s, cbstr host, cbstr port)
{
    int r = BAL_FALSE;

    if ((s) && _bal_validstr(host) && _bal_validstr(port)) {
        bal_addrinfo ai;

        memset(&ai, 0L, sizeof(ai));

        if (BAL_TRUE == _bal_getaddrinfo(0, s->af, s->st, host, port, &ai)) {
            bal_addrlist al;

            memset(&al, 0, sizeof(al));

            if (BAL_TRUE == _bal_aitoal(&ai, &al)) {
                r = bal_connectaddrlist((balst*)s, &al);

                bal_freeaddrlist(&al);
            }

            freeaddrinfo(ai._ai);
        }
    }

    return r;
}

int bal_connectaddrlist(balst* s, bal_addrlist* al)
{
    int r = BAL_FALSE;

    if (s) {
        if (BAL_TRUE == bal_resetaddrlist(al)) {
            const bal_sockaddr* sa = NULL;

            while (NULL != (sa = bal_enumaddrlist(al))) {
                r = connect(s->sd, (const struct sockaddr*)sa, BAL_SASIZE((*sa)));

#if defined(_WIN32)

                if ((!r) || ((-1 == r) && (WSAEWOULDBLOCK == WSAGetLastError()))) {
#else
                if ((!r) || ((-1 == r) && (EWOULDBLOCK == r))) {

#endif /* !_WIN32 */

                    s->_f |= BAL_F_PENDCONN;
                    r = BAL_TRUE;
                    break;
                }
            }
        }
    }

    return r;
}

int bal_send(const balst* s, const void* data, size_t len, int flags)
{
    if ((s) && (data) && (len))
        return send(s->sd, (const char*)data, len, flags);
    else
        return BAL_FALSE;
}

int bal_recv(const balst* s, void* data, size_t len, int flags)
{
    if ((s) && (data) && (len))
        return recv(s->sd, (char*)data, len, flags);
    else
        return BAL_FALSE;
}

int bal_sendto(const balst* s, cbstr host, cbstr port, const void* data, size_t len, int flags)
{
    int r = BAL_FALSE;

    if ((s) && _bal_validstr(host) && _bal_validstr(port) && (data) && (len)) {
        bal_addrinfo ai;

        memset(&ai, 0, sizeof(ai));

        if (BAL_TRUE == _bal_getaddrinfo(0, PF_UNSPEC, SOCK_DGRAM, host, port, &ai)) {
            r = bal_sendtoaddr(s, (const bal_sockaddr*)ai._ai->ai_addr, data, len, flags);

            freeaddrinfo(ai._ai);
        }
    }

    return r;
}

int bal_sendtoaddr(const balst* s, const bal_sockaddr* sa, const void* data, size_t len, int flags)
{
    if ((s) && (sa) && (data) && (len))
        return sendto(s->sd, data, len, flags, (const struct sockaddr*)sa, BAL_SASIZE((*sa)));
    else
        return BAL_FALSE;
}

int bal_recvfrom(const balst* s, void* data, size_t len, int flags, bal_sockaddr* res)
{
    if ((s) && (data) && (len)) {
        socklen_t sasize = sizeof(bal_sockaddr);

        return recvfrom(s->sd, (char*)data, len, flags, (struct sockaddr*)res, &sasize);

    } else {
        return BAL_FALSE;
    }
}

int bal_bind(const balst* s, cbstr addr, cbstr port)
{
    int r = BAL_FALSE;

    if ((s) && _bal_validstr(addr) && _bal_validstr(port)) {
        bal_addrinfo ai;

        memset(&ai, 0, sizeof(ai));

        if (BAL_TRUE == _bal_getaddrinfo(AI_NUMERICHOST, s->af, s->st, addr, port, &ai)) {
            const struct addrinfo* a = NULL;

            while (NULL != (a = _bal_enumaddrinfo(&ai))) {
                if (0L == bind(s->sd, (const struct sockaddr*)a->ai_addr, a->ai_addrlen)) {
                    r = BAL_TRUE;
                    break;
                }
            }

            freeaddrinfo(ai._ai);
        }
    }

    return r;
}

int bal_listen(const balst* s, int backlog)
{
    return ((NULL != s) ? listen(s->sd, backlog) : BAL_FALSE);
}

int bal_accept(const balst* s, balst* res, bal_sockaddr* resaddr)
{
    int r = BAL_FALSE;

    if ((s) && (res) && (resaddr)) {
        socklen_t sasize = sizeof(bal_sockaddr);

        if (-1L != (res->sd = accept(s->sd, (struct sockaddr*)resaddr, &sasize)))
            r = BAL_TRUE;
    }

    return r;
}

int bal_getoption(const balst* s, int level, int name, void* optval, socklen_t len)
{
    return ((NULL != s) ? getsockopt(s->sd, level, name, optval, &len) : BAL_FALSE);
}

int bal_setoption(const balst* s, int level, int name, const void* optval, socklen_t len)
{
    return ((NULL != s) ? setsockopt(s->sd, level, name, optval, len) : BAL_FALSE);
}

int bal_setbroadcast(const balst* s, int flag)
{
    return bal_setoption(s, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(int));
}

int bal_getbroadcast(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_BROADCAST, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setdebug(const balst* s, int flag)
{
    return bal_setoption(s, SOL_SOCKET, SO_DEBUG, &flag, sizeof(int));
}

int bal_getdebug(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_DEBUG, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setlinger(const balst* s, int sec)
{
    struct linger l;

    l.l_onoff  = (0 != sec) ? 1 : 0;
    l.l_linger = sec;

    return bal_setoption(s, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
}

int bal_getlinger(const balst* s, int* sec)
{
    int r = BAL_FALSE;

    if (sec) {
        struct linger l;

        memset(&l, 0, sizeof(l));

        if (BAL_TRUE == bal_getoption(s, SOL_SOCKET, SO_LINGER, &l, sizeof(l))) {
            *sec = l.l_linger;
            r    = BAL_TRUE;
        }
    }

    return r;
}

int bal_setkeepalive(const balst* s, int flag)
{
    return bal_setoption(s, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(int));
}

int bal_getkeepalive(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_KEEPALIVE, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setoobinline(const balst* s, int flag)
{
    return bal_setoption(s, SOL_SOCKET, SO_OOBINLINE, &flag, sizeof(int));
}

int bal_getoobinline(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_OOBINLINE, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setreuseaddr(const balst* s, int flag)
{
    return bal_setoption(s, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
}

int bal_getreuseaddr(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setsendbufsize(const balst* s, int size)
{
    return bal_setoption(s, SOL_SOCKET, SO_SNDBUF, &size, sizeof(int));
}

int bal_getsendbufsize(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_SNDBUF, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setrecvbufsize(const balst* s, int size)
{
    return bal_setoption(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof(int));
}

int bal_getrecvbufsize(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_RCVBUF, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_setsendtimeout(const balst* s, long sec, long msec)
{
    struct timeval t;

    t.tv_sec  = sec;
    t.tv_usec = msec;

    return bal_setoption(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
}

int bal_getsendtimeout(const balst* s, long* sec, long* msec)
{
    int r = BAL_FALSE;

    if ((sec) && (msec)) {
        struct timeval t;

        memset(&t, 0, sizeof(t));

        if (BAL_TRUE == bal_getoption(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t))) {
            *sec  = t.tv_sec;
            *msec = t.tv_usec;
            r     = BAL_TRUE;
        }
    }

    return r;
}

int bal_setrecvtimeout(const balst* s, long sec, long msec)
{
    struct timeval t;

    t.tv_sec  = sec;
    t.tv_usec = msec;

    return bal_setoption(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
}

int bal_getrecvtimeout(const balst* s, long* sec, long* msec)
{
    int r = BAL_FALSE;

    if ((sec) && (msec)) {
        struct timeval t;

        memset(&t, 0, sizeof(t));

        if (BAL_TRUE == bal_getoption(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t))) {
            *sec  = t.tv_sec;
            *msec = t.tv_usec;
            r     = BAL_TRUE;
        }
    }

    return r;
}

int bal_geterror(const balst* s)
{
    int r = BAL_FALSE;

    if (BAL_FALSE == bal_getoption(s, SOL_SOCKET, SO_ERROR, &r, sizeof(int)))
        r = BAL_FALSE;

    return r;
}

int bal_islistening(const balst* s)
{
    int r = BAL_FALSE;
    int l = 0;

    if ((BAL_TRUE == bal_getoption(s, SOL_SOCKET, SO_ACCEPTCONN, &l, sizeof(int))) && (l))
        r = BAL_TRUE;

    return r;
}

int bal_isreadable(const balst* s)
{
    int r = BAL_FALSE;

    if (s) {
        fd_set fd;
        struct timeval t;

        FD_ZERO(&fd);
        FD_SET(s->sd, &fd);

        t.tv_sec  = 0L;
        t.tv_usec = 0L;

        if (0 == select(1, &fd, NULL, NULL, &t)) {
            if (FD_ISSET(s->sd, &fd))
                r = BAL_TRUE;
        }
    }

    return r;
}

int bal_iswritable(const balst* s)
{
    int r = BAL_FALSE;

    if (s) {
        fd_set fd;
        struct timeval t;

        FD_ZERO(&fd);
        FD_SET(s->sd, &fd);

        t.tv_sec  = 0L;
        t.tv_usec = 0L;

        if (0 == select(1, NULL, &fd, NULL, &t)) {
            if (FD_ISSET(s->sd, &fd))
                r = BAL_TRUE;
        }
    }

    return r;
}

int bal_setiomode(const balst* s, unsigned long flag)
{
#if defined(_WIN32)
    return ((NULL != s) ? ioctlsocket(s->sd, FIONBIO, &flag) : BAL_FALSE);
#else
    return ((NULL != s) ? ioctl(s->sd, FIONBIO, &flag) : BAL_FALSE);
#endif
}

size_t bal_recvqueuesize(const balst* s)
{
    size_t r = 0UL;

    if (s) {
#if defined(_WIN32)
        if (0 != ioctlsocket(s->sd, FIONREAD, (void*)&r))
            r = 0U;
#else
        if (0 != ioctl(s->sd, FIONREAD, &r))
            r = 0U;
#endif
    }

    return r;
}

int bal_lastliberror(bal_error* err)
{
    return _bal_getlasterror(NULL, err);
}

int bal_lastsockerror(const balst* s, bal_error* err)
{
    return _bal_getlasterror(s, err);
}

int bal_resolvehost(cbstr host, bal_addrlist* out)
{
    int r = BAL_FALSE;

    if (_bal_validstr(host) && (out)) {
        bal_addrinfo ai;

        memset(&ai, 0, sizeof(ai));

        if (BAL_TRUE == _bal_getaddrinfo(0, PF_UNSPEC, SOCK_STREAM, host, NULL, &ai)) {
            if (BAL_TRUE == _bal_aitoal(&ai, out))
                r = BAL_TRUE;

            freeaddrinfo(ai._ai);
        }
    }

    return r;
}

int bal_getremotehostaddr(const balst* s, bal_sockaddr* out)
{
    int r = BAL_FALSE;

    if ((s) && (out)) {
        socklen_t salen = sizeof(bal_sockaddr);

        memset(out, 0, sizeof(bal_sockaddr));

        if (0 == getpeername(s->sd, (struct sockaddr*)out, &salen))
            r = BAL_TRUE;
    }

    return r;
}

int bal_getremotehoststrings(const balst* s, int dns, bal_addrstrings* out)
{
    int r = BAL_FALSE;
    bal_sockaddr sa;

    if (BAL_TRUE == bal_getremotehostaddr(s, &sa))
        r = bal_getaddrstrings(&sa, dns, out);

    return r;
}

int bal_getlocalhostaddr(const balst* s, bal_sockaddr* out)
{
    int r = BAL_FALSE;

    if ((s) && (out)) {
        socklen_t salen = sizeof(bal_sockaddr);

        memset(out, 0, sizeof(bal_sockaddr));

        if (0 == getsockname(s->sd, (struct sockaddr*)out, &salen))
            r = BAL_TRUE;
    }

    return r;
}

int bal_getlocalhoststrings(const balst* s, int dns, bal_addrstrings* out)
{
    int r = BAL_FALSE;
    bal_sockaddr sa;

    if (BAL_TRUE == bal_getlocalhostaddr(s, &sa))
        r = bal_getaddrstrings(&sa, dns, out);

    return r;
}

int bal_resetaddrlist(bal_addrlist* al)
{
    int r = BAL_FALSE;

    if (al) {
        al->_p = al->_a;
        r      = BAL_TRUE;
    }

    return r;
}

const bal_sockaddr* bal_enumaddrlist(bal_addrlist* al)
{
    const bal_sockaddr* r = NULL;

    if (al) {
        if (al->_a) {
            if (al->_p) {
                r      = &al->_p->_sa;
                al->_p = al->_p->_n;
            } else {
                bal_resetaddrlist(al);
            }
        }
    }

    return r;
}

int bal_freeaddrlist(bal_addrlist* al)
{
    int r = BAL_FALSE;

    if (al) {
        bal_addr* a = al->_p = al->_a;

        while (al->_p) {
            a = al->_p->_n;
            free(al->_p);
            al->_p = a;
        }

        r      = BAL_TRUE;
        al->_p = al->_a = NULL;
    }

    return r;
}

int bal_getaddrstrings(const bal_sockaddr* in, int dns, bal_addrstrings* out)
{
    int r = BAL_FALSE;

    if ((in) && (out)) {
        memset(out, 0, sizeof(bal_addrstrings));

        if (BAL_TRUE == _bal_getnameinfo(BAL_NI_NODNS, in, out->ip, out->port)) {
            if (dns) {
                if (BAL_FALSE == _bal_getnameinfo(BAL_NI_DNS, in, out->host, out->port)) {
#ifdef BAL_USE_WCHAR
                    wcscpy(out->host, BAL_AS_UNKNWN);
#else
                    strcpy(out->host, BAL_AS_UNKNWN);
#endif
                }
            }

            if (PF_INET == ((struct sockaddr*)in)->sa_family)
                out->type = BAL_AS_IPV4;
            else if (PF_INET6 == ((struct sockaddr*)in)->sa_family)
                out->type = BAL_AS_IPV6;
            else
                out->type = BAL_AS_UNKNWN;

            r = BAL_TRUE;
        }
    }

    return r;
}


/*
  Internal functions
 */


int _bal_getaddrinfo(int f, int af, int st, cbstr host, cbstr port, bal_addrinfo* res)
{
    int r = BAL_FALSE;

    if (_bal_validstr(host) && (res)) {
        const char* mbhost = _bal_getmbstr(host);
        const char* mbport = _bal_getmbstr(port);

        if (mbhost) {
            struct addrinfo hints;

            memset(&hints, 0, sizeof(hints));

            hints.ai_flags    = f;
            hints.ai_family   = af;
            hints.ai_socktype = st;

            r = getaddrinfo(mbhost, mbport, (const struct addrinfo*)&hints, &res->_ai);

            if (!r)
                res->_p = res->_ai;
            else
                res->_p = res->_ai = NULL;

#ifdef BAL_USE_WCHAR
            free((void*)mbhost);

            if (mbport)
                free((void*)mbport);
#endif
        }
    }

    if (BAL_TRUE != r) {
        if (BAL_FALSE != r) {
            _bal_setlasterror(r);
            r = BAL_FALSE;
        }
    }

    return r;
}

int _bal_getnameinfo(int f, const bal_sockaddr* in, bstr host, bstr port)
{
    int r = BAL_FALSE;

    if ((in) && (host)) {
#ifdef BAL_USE_WCHAR
        char mbhost[NI_MAXHOST] = {0};
        char mbport[NI_MAXSERV] = {0};
#else
        char* mbhost = host;
        char* mbport = port;
#endif

        socklen_t inlen = BAL_SASIZE((*in));

        r = getnameinfo((const struct sockaddr*)in, inlen, mbhost, NI_MAXHOST, mbport, NI_MAXSERV,
            f);

#ifdef BAL_USE_WCHAR

        if (!r) {
            r = BAL_FALSE;

            if (0 == _bal_retstr(host, (const char*)mbhost)) {
                if (port) {
                    if (0 == _bal_retstr(port, (const char*)mbport))
                        r = BAL_TRUE;
                } else {
                    r = BAL_TRUE;
                }
            }
        }

#endif
    }

    if (BAL_TRUE != r) {
        if (BAL_FALSE != r) {
            _bal_setlasterror(r);
            r = BAL_FALSE;
        }
    }

    return r;
}

const struct addrinfo* _bal_enumaddrinfo(bal_addrinfo* ai)
{
    const struct addrinfo* r = NULL;

    if (ai) {
        if (ai->_ai) {
            if (ai->_p) {
                r      = ai->_p;
                ai->_p = ai->_p->ai_next;
            } else {
                ai->_p = ai->_ai;
            }
        }
    }

    return r;
}

int _bal_aitoal(bal_addrinfo* in, bal_addrlist* out)
{
    int r = BAL_FALSE;

    if ((in) && (in->_ai) && (out)) {
        const struct addrinfo* ai = NULL;
        bal_addr** a              = &out->_a;
        r                         = BAL_TRUE;

        in->_p = in->_ai;

        while (NULL != (ai = _bal_enumaddrinfo(in))) {
            *a = (bal_addr*)calloc(1UL, sizeof(bal_addr));

            if (!(*a)) {
                r = BAL_FALSE;
                break;
            }

            memcpy(&((*a)->_sa), ai->ai_addr, ai->ai_addrlen);

            a = &((*a)->_n);
        }

        bal_resetaddrlist(out);
    }

    return r;
}

int _bal_getlasterror(const balst* s, bal_error* err)
{
    int r = BAL_FALSE;

    if (err) {
        memset(err, 0L, sizeof(bal_error));

        if (s) {
            err->code = bal_geterror(s);

        } else {
#if defined(_WIN32)
            err->code = WSAGetLastError();
#else
            err->code = errno;
#endif
        }

#if defined(_WIN32)

        if (0U != FormatMessage(0x00001200U, NULL, err->code, 0U, err->desc, BAL_MAXERROR, NULL))
            r = BAL_TRUE;
#else

        if (err->code == EAI_AGAIN || err->code == EAI_BADFLAGS || err->code == EAI_FAIL ||
            err->code == EAI_FAMILY || err->code == EAI_MEMORY || err->code == EAI_NONAME ||
            err->code == EAI_NODATA || err->code == EAI_SERVICE || err->code == EAI_SOCKTYPE) {
            if (0 == _bal_retstr(err->desc, gai_strerror(err->code)))
                r = BAL_TRUE;

        } else {
            if (0 == _bal_retstr(err->desc, (const char*)strerror(err->code)))
                r = BAL_TRUE;
        }

#endif /* !_WIN32 */
    }

    return r;
}

void _bal_setlasterror(int err)
{
#if defined(_WIN32)
    WSASetLastError(err);
#else
    errno = err;
#endif
}

const char* _bal_getmbstr(cbstr input)
{
#ifdef BAL_USE_WCHAR

    char* mbstr = (char*)calloc(sizeof(char), (wcslen(input) + sizeof(char)));

    if (!mbstr) {
        return (char*)(NULL);
    } else if (((size_t)-1) == wcstombs(mbstr, input, wcslen(input))) {
        free(mbstr);
        return (char*)(NULL);
    } else {
        return mbstr;
    }
#else
    return input;
#endif /* !BAL_USE_WCHAR* / \
} \
 \ \
int _bal_retstr(bstr out, const char* in) { \
    int r = BAL_FALSE; \
 \ \
#ifdef BAL_USE_WCHAR \
    if (((size_t)-1) != mbstowcs(out, in, strlen(in))) \
        r = BAL_TRUE; \
#else \
    strcpy(out, in); \
    r = BAL_TRUE; \
#endif /* !BAL_USE_WCHAR* / \
 \ \
    return r; \
} \
 \ \
int _bal_haspendingconnect(const balst* s) { \
    int r = BAL_FALSE; \
 \ \
    if (s->_f & BAL_F_PENDCONN) \
        r = BAL_TRUE; \
 \ \
    return r; \
} \
 \ \
int _bal_isclosedcircuit(const balst* s) { \
    int r = BAL_FALSE; \
 \ \
    if (s) { \
        unsigned char buf; \
 \ \
        int rcv = recv(s->sd, &buf, sizeof(unsigned char), MSG_PEEK); \
 \ \
        if (0 == rcv) \
            r = BAL_TRUE; \
 \ \
        if (-1 == rcv) { \
#if defined(_WIN32) \
 \ \
            int error = WSAGetLastError(); \
 \ \
            if ((WSAENETDOWN == error) || (WSAENOTCONN == error) || (WSAEOPNOTSUPP == error) || \
                (WSAESHUTDOWN == error) || (WSAECONNABORTED == error) || (WSAECONNRESET == error)) \
{ r = BAL_TRUE; \
            } \
 \ \
#else \
 \ \
            if ((EBADF == errno) || (ENOTCONN == errno) || (ENOTSOCK == errno)) \
                r = BAL_TRUE; \
 \ \
#endif /* !_WIN32 */
}
}

return r;
}

BALTHREADAPI _bal_eventthread(void* p)
{
    bal_eventthread_data* td = (bal_eventthread_data*)p;

    if (td) {
        bal_selectdata* t = NULL;
        fd_set r;
        fd_set w;
        fd_set e;

        while (0 == td->die) {
            if (BAL_TRUE == _bal_mutex_lock(td->m)) {
                int size = _bal_sdl_size(td->l);

                if (0 < size) {
                    struct timeval tv = {0, 0};
                    int ret           = -1;
                    bal_socket highsd = (bal_socket)-1;

                    FD_ZERO(&r);
                    FD_ZERO(&w);
                    FD_ZERO(&e);

                    _bal_sdl_reset(td->l);

                    while (BAL_TRUE == _bal_sdl_enum(td->l, &t)) {
                        if (BAL_TRUE == _bal_haspendingconnect(t->s)) {
                            t->mask |= BAL_S_CONNECT;
                            t->s->_f &= ~BAL_F_PENDCONN;
                        }

                        if (t->s->sd > highsd)
                            highsd = t->s->sd;

                        FD_SET(t->s->sd, &r);
                        FD_SET(t->s->sd, &w);
                        FD_SET(t->s->sd, &e);
                    }

                    ret = select((int)(highsd + 1), &r, &w, &e, &tv);

                    if ((-1 != ret)) {
                        _bal_dispatchevents(&r, td, BAL_S_READ);
                        _bal_dispatchevents(&w, td, BAL_S_WRITE);
                        _bal_dispatchevents(&e, td, BAL_S_EXCEPT);
                    }
                }

                _bal_mutex_unlock(td->m);
            }

            FD_ZERO(&r);
            FD_ZERO(&w);
            FD_ZERO(&e);

#if defined(_WIN32)
            Sleep(1);
#else
            sched_yield();
#endif
        }

    } else {
#if defined(_WIN32)
        return 1;
#else
        return (void*)(1);
#endif
    }

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

int _bal_initasyncselect(bal_thread* t, bal_mutex* m, bal_eventthread_data* td)
{
    int r = BAL_FALSE;

    if ((t) && (m) && (td)) {
        td->die = 0;

        if (BAL_TRUE == _bal_mutex_init(m)) {
#if defined(_WIN32)

            DWORD id;

            if (NULL != ((*t) = CreateThread(NULL, 0, _Bal_EventThread, td, 0, &id)))
                r = BAL_TRUE;

#else

            if (0 == pthread_create(t, NULL, _bal_eventthread, td))
                r = BAL_TRUE;

#endif /* _WIN32 */
        }
    }

    return r;
}

void _bal_dispatchevents(fd_set* set, bal_eventthread_data* td, int type)
{
    if ((set) && (td)) {
        bal_selectdata* t = NULL;

        _bal_sdl_reset(td->l);

        while (BAL_TRUE == _bal_sdl_enum(td->l, &t)) {
            int event = 0;
            int snd   = 0;

            if (0 != FD_ISSET(t->s->sd, set)) {
                switch (type) {
                    case BAL_S_READ:

                        if (t->mask & BAL_E_READ) {
                            if (BAL_TRUE == _bal_isclosedcircuit(t->s))

                                event = BAL_E_CLOSE;

                            else if (BAL_TRUE == bal_islistening(t->s))

                                event = BAL_E_ACCEPT;

                            else

                                event = BAL_E_READ;

                            snd = 1;
                        }

                        break;
                    case BAL_S_WRITE:

                        if (t->mask & BAL_E_WRITE) {
                            if (t->mask & BAL_S_CONNECT) {
                                event = BAL_E_CONNECT;
                                t->mask &= ~BAL_S_CONNECT;

                            } else {
                                event = BAL_E_WRITE;
                            }

                            snd = 1;
                        }

                        break;
                    case BAL_S_EXCEPT:

                        if (t->mask & BAL_S_CONNECT) {
                            event = BAL_E_CONNFAIL;
                            t->mask &= ~BAL_S_CONNECT;


                        } else {
                            event = BAL_E_EXCEPTION;
                        }

                        snd = 1;

                        break;
                }

                if (1 == snd)

                    t->proc(t->s, event);

                if (BAL_E_CLOSE == event)

                    _bal_sdl_rem(td->l, t->s->sd);
            }
        }
    }
}

int _bal_sdl_add(bal_selectdata_list* l, const bal_selectdata* d)
{
    int r = BAL_FALSE;

    if ((l) && (d)) {
        bal_selectdata** t = NULL;
        bal_selectdata* n  = NULL;

        if (l->_h) {
            t = &l->_t->_n;
            n = l->_t;
        } else {
            t = &l->_h;
        }

        (*t) = (bal_selectdata*)malloc(sizeof(bal_selectdata));

        if ((*t)) {
            memcpy((*t), d, sizeof(bal_selectdata));

            (*t)->_n = NULL;
            (*t)->_p = n;
            l->_t    = (*t);
            r        = BAL_TRUE;
        }
    }

    return r;
}

int _bal_sdl_rem(bal_selectdata_list* l, bal_socket sd)
{
    int r = BAL_FALSE;

    if ((l) && (sd)) {
        bal_selectdata* t = _bal_sdl_find(l, sd);

        if (t) {
            if (t == l->_h) {
                l->_h = t->_n;

            } else {
                t->_p->_n = (t->_n ? t->_n : NULL);

                if (t == l->_t)

                    l->_t = t->_p;

                else

                    t->_n->_p = (t->_p ? t->_p : NULL);
            }

            free(t);
            r = BAL_TRUE;
        }
    }

    return r;
}

int _bal_sdl_clr(bal_selectdata_list* l)
{
    int r = BAL_FALSE;

    if (l) {
        if (l->_h) {
            bal_selectdata* t  = l->_h;
            bal_selectdata* t2 = NULL;

            while (t) {
                t2 = t->_n;

                free(t);

                t = t2;
            }

            l->_h = l->_t = l->_c = NULL;
            r                     = BAL_TRUE;
        }
    }

    return r;
}

int _bal_sdl_size(bal_selectdata_list* l)
{
    int r = 0;

    if (l) {
        bal_selectdata* d = l->_h;

        while (d) {
            d = d->_n;
            r++;
        }
    }

    return r;
}

int _bal_sdl_copy(bal_selectdata_list* dest, bal_selectdata_list* src)
{
    int r = BAL_FALSE;

    if ((dest) && (src)) {
        bal_selectdata* d = src->_h;
        int copied        = 0;

        _bal_sdl_clr(dest);
        _bal_sdl_reset(src);

        while (d) {
            if (BAL_TRUE == _bal_sdl_add(dest, d))

                copied++;

            d = d->_n;
        }

        if (0 < copied)

            r = BAL_TRUE;
    }

    return r;
}

int _bal_sdl_enum(bal_selectdata_list* l, bal_selectdata** d)
{
    int r = BAL_FALSE;

    if ((l) && (d)) {
        if (l->_c) {
            *d    = l->_c;
            l->_c = l->_c->_n;
            r     = BAL_TRUE;

        } else {
            _bal_sdl_reset(l);
        }
    }

    return r;
}

void _bal_sdl_reset(bal_selectdata_list* l)
{
    if (l)
        l->_c = l->_h;
}

bal_selectdata* _bal_sdl_find(const bal_selectdata_list* l, bal_socket sd)
{
    bal_selectdata* r = NULL;

    if ((l) && (sd)) {
        bal_selectdata* t = l->_h;

        while (t) {
            if (t->s->sd == sd) {
                r = t;
                break;
            }

            t = t->_n;
        }
    }

    return r;
}

int _bal_mutex_init(bal_mutex* m)
{
    int r = BAL_FALSE;

    if (m) {
#if defined(_WIN32)

        *m = CreateMutex(NULL, FALSE, NULL);

        if (*m)
            r = BAL_TRUE;

#else

        if (0 == pthread_mutex_init(m, NULL))
            r = BAL_TRUE;

#endif /* !_WIN32 */
    }

    return r;
}

int _bal_mutex_lock(bal_mutex* m)
{
    int r = BAL_FALSE;

    if (m) {
#if defined(_WIN32)

        if (WAIT_OBJECT_0 == WaitForSingleObject(*m, INFINITE))
            r = BAL_TRUE;

#else

        if (0 == pthread_mutex_lock(m))
            r = BAL_TRUE;

#endif /* !_WIN32 */
    }

    return r;
}

int _bal_mutex_unlock(bal_mutex* m)
{
    int r = BAL_FALSE;

    if (m) {
#if defined(_WIN32)

        if (ReleaseMutex(*m))
            r = BAL_TRUE;

#else

        if (0 == pthread_mutex_unlock(m))
            r = BAL_TRUE;

#endif /* !_WIN32 */
    }

    return r;
}

int _bal_mutex_free(bal_mutex* m)
{
    int r = BAL_FALSE;

    if (m) {
#if defined(_WIN32)
        if (*m && INVALID_HANDLE_VALUE != *m) {
            if (CloseHandle(*m))
                r = BAL_TRUE;
            *m = NULL;
        }
#else
        if (0 == pthread_mutex_destroy(m))
            r = BAL_TRUE;
#endif /* !_WIN32 */
    }

    return r;
}