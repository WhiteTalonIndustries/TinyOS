#ifndef TINYOS_LWIPOPTS_H
#define TINYOS_LWIPOPTS_H

/* Minimal NO_SYS=1 (polling, no RTOS/background IRQ thread) station-mode
 * config -- matches how usb.c already pumps TinyUSB from the shell's own
 * blocking read loop (wifi.c's wifi_poll() does the same for cyw43/lwIP).
 * Values are the standard ones used across pico-sdk's own Pico W wireless
 * examples for this exact NO_SYS + cyw43_arch_lwip_poll combination. */

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1

#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define MEM_STATS                   0
#define SYS_STATS                   0
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

#define LWIP_CHKSUM_ALGORITHM       3

#ifndef NDEBUG
#define LWIP_DEBUG                  0
#endif

#endif
