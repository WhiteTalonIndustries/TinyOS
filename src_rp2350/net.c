#include "net.h"
#include "wifi.h"
#include "usb.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/icmp.h"
#include <stdio.h>
#include <string.h>

/* Single-shot ping and a raw-HTTP GET ("browser"), both built directly on
 * lwIP's callback-based raw API (this is NO_SYS=1 lwIP -- see
 * lwipopts.h/wifi.c) and driven with a simple spin-on-wifi_poll() wait
 * loop, matching how usb.c already blocks on console_getc(). Only one of
 * either runs at a time (TinyOS's shell is single-threaded), so plain
 * static/global state is fine -- no locking needed. */

#define WAIT_TICK_US 500

static int wait_until(volatile bool *done, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!*done) {
        wifi_poll();
        if (time_reached(deadline)) return -1;
        busy_wait_us(WAIT_TICK_US);
    }
    return 0;
}

static int resolve(const char *host, ip_addr_t *out) {
    static ip_addr_t resolved;
    err_t err;
    int i;

    if (ipaddr_aton(host, out)) return 0;

    /* dns_gethostbyname() with a NULL found-callback only ever returns
     * synchronously: ERR_OK if already cached, ERR_INPROGRESS if a query
     * was just started (or already pending) with no way to be notified
     * when it lands. Poll by re-issuing the same call -- lwIP's resolver
     * merges duplicate in-flight queries for the same hostname rather
     * than spamming the network, so this just checks the cache until the
     * real query (kicked off by the first call) fills it in. */
    for (i = 0; i < 40; i++) {
        err = dns_gethostbyname(host, &resolved, NULL, NULL);
        if (err == ERR_OK) {
            *out = resolved;
            return 0;
        }
        if (err != ERR_INPROGRESS) return -1;
        wifi_poll();
        busy_wait_us(100 * 1000);
    }
    return -1;
}

/* ---------------- ping ---------------- */

#define PING_ID       0xAFAF
#define PING_DATA_LEN 32

static volatile bool ping_got_reply;
static u16_t ping_seqno;
static absolute_time_t ping_sent_at;

static u8_t ping_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct icmp_echo_hdr *iecho;
    (void)arg;
    (void)pcb;
    (void)addr;

    if (p->tot_len >= IP_HLEN + sizeof(struct icmp_echo_hdr) && pbuf_remove_header(p, IP_HLEN) == 0) {
        iecho = (struct icmp_echo_hdr *)p->payload;
        if (iecho->id == PING_ID && iecho->seqno == lwip_htons(ping_seqno)) {
            ping_got_reply = true;
            pbuf_free(p);
            return 1;
        }
        pbuf_add_header(p, IP_HLEN);
    }
    return 0;
}

void net_ping(const char *host) {
    ip_addr_t target;
    struct raw_pcb *pcb;
    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    size_t i;

    if (!wifi_is_connected()) {
        printf("ping: not connected -- run 'wifi connect' first\n");
        return;
    }
    if (resolve(host, &target) != 0) {
        printf("ping: could not resolve %s\n", host);
        return;
    }

    pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) {
        printf("ping: out of memory\n");
        return;
    }
    raw_recv(pcb, ping_recv_cb, NULL);
    raw_bind(pcb, IP_ADDR_ANY);

    ping_seqno++;
    ping_got_reply = false;

    p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + PING_DATA_LEN, PBUF_RAM);
    if (!p) {
        printf("ping: out of memory\n");
        raw_remove(pcb);
        return;
    }
    iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id = PING_ID;
    iecho->seqno = lwip_htons(ping_seqno);
    for (i = 0; i < PING_DATA_LEN; i++) {
        ((char *)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
    }
    iecho->chksum = inet_chksum(iecho, (u16_t)(sizeof(struct icmp_echo_hdr) + PING_DATA_LEN));

    printf("PING %s (%s)\n", host, ipaddr_ntoa(&target));
    ping_sent_at = get_absolute_time();
    raw_sendto(pcb, p, &target);
    pbuf_free(p);

    if (wait_until(&ping_got_reply, 3000) == 0) {
        int64_t us = absolute_time_diff_us(ping_sent_at, get_absolute_time());
        printf("Reply from %s: time=%lldms\n", ipaddr_ntoa(&target), (long long)(us / 1000));
    } else {
        printf("Request timed out.\n");
    }

    raw_remove(pcb);
}

/* ---------------- browser (raw HTTP/1.0 GET) ---------------- */

#define HTTP_BUF_SIZE 8192

static char http_buf[HTTP_BUF_SIZE];
static uint32_t http_len;
static volatile bool http_done;
static volatile bool http_connect_failed;
static char http_request[512];

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;
    (void)err;
    if (!p) {
        http_done = true;
        tcp_close(tpcb);
        return ERR_OK;
    }
    {
        struct pbuf *q;
        for (q = p; q; q = q->next) {
            uint32_t take = q->len;
            if (http_len + take >= HTTP_BUF_SIZE) take = HTTP_BUF_SIZE - 1 - http_len;
            if (take > 0) {
                memcpy(http_buf + http_len, q->payload, take);
                http_len += take;
            }
        }
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err) {
    (void)arg;
    (void)err;
    http_connect_failed = true;
    http_done = true;
}

static err_t http_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) {
        http_connect_failed = true;
        http_done = true;
        return err;
    }
    tcp_write(tpcb, http_request, (u16_t)strlen(http_request), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    return ERR_OK;
}

void net_get(const char *host, const char *path) {
    ip_addr_t target;
    struct tcp_pcb *pcb;

    if (!wifi_is_connected()) {
        printf("browser: not connected -- run 'wifi connect' first\n");
        return;
    }
    if (!path || path[0] == '\0') path = "/";

    if (resolve(host, &target) != 0) {
        printf("browser: could not resolve %s\n", host);
        return;
    }

    pcb = tcp_new();
    if (!pcb) {
        printf("browser: out of memory\n");
        return;
    }

    http_len = 0;
    http_done = false;
    http_connect_failed = false;
    snprintf(http_request, sizeof(http_request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, http_recv_cb);
    tcp_err(pcb, http_err_cb);

    printf("Connecting to %s (%s)...\n", host, ipaddr_ntoa(&target));
    if (tcp_connect(pcb, &target, 80, http_connected_cb) != ERR_OK) {
        printf("browser: connect failed\n");
        return;
    }

    if (wait_until(&http_done, 8000) != 0) {
        printf("browser: request timed out\n");
        tcp_close(pcb);
        return;
    }
    if (http_connect_failed && http_len == 0) {
        printf("browser: connection error\n");
        return;
    }

    console_write(http_buf, http_len);
    printf("\n");
}
