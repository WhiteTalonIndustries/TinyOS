#include "wifi.h"
#include "fs.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

#define WIFI_CFG_FILE   "WIFI.CFG"
#define CONNECT_TIMEOUT_MS 15000

static bool connected;
static char ssid_buf[64];

static int read_wifi_cfg(char *ssid_out, int ssid_cap, char *pass_out, int pass_cap) {
    char buf[256];
    char *line1, *line2, *nl;
    size_t l;
    int n = fs_read(WIFI_CFG_FILE, buf, sizeof(buf));
    if (n < 0) return -1;

    line1 = buf;
    nl = strchr(buf, '\n');
    if (!nl) return -1;
    *nl = '\0';
    line2 = nl + 1;
    nl = strchr(line2, '\n');
    if (nl) *nl = '\0';

    l = strlen(line1);
    if (l && line1[l - 1] == '\r') line1[l - 1] = '\0';
    l = strlen(line2);
    if (l && line2[l - 1] == '\r') line2[l - 1] = '\0';

    if (line1[0] == '\0') return -1;

    strncpy(ssid_out, line1, (size_t)ssid_cap - 1);
    ssid_out[ssid_cap - 1] = '\0';
    strncpy(pass_out, line2, (size_t)pass_cap - 1);
    pass_out[pass_cap - 1] = '\0';
    return 0;
}

void wifi_init(void) {
    char pass_buf[64];
    int rc;

    connected = false;
    if (read_wifi_cfg(ssid_buf, sizeof(ssid_buf), pass_buf, sizeof(pass_buf)) != 0) {
        printf("wifi: no WIFI.CFG on the SD card (expected 2 lines: SSID then password) -- skipping\n");
        return;
    }

    if (cyw43_arch_init()) {
        printf("wifi: cyw43_arch_init failed\n");
        return;
    }
    cyw43_arch_enable_sta_mode();

    printf("wifi: connecting to \"%s\"...\n", ssid_buf);
    rc = cyw43_arch_wifi_connect_timeout_ms(ssid_buf, pass_buf, CYW43_AUTH_WPA2_AES_PSK, CONNECT_TIMEOUT_MS);
    if (rc) {
        printf("wifi: connect failed (%d)\n", rc);
        return;
    }

    connected = true;
    printf("wifi: connected\n");
}

bool wifi_is_connected(void) {
    return connected;
}

void wifi_poll(void) {
    if (connected) cyw43_arch_poll();
}

void wifi_print_status(void) {
    struct netif *nif;
    if (!connected) {
        printf("WiFi: not connected\n");
        return;
    }
    nif = netif_default;
    if (!nif) {
        printf("WiFi: connected to \"%s\", no interface yet\n", ssid_buf);
        return;
    }
    printf("WiFi: connected to \"%s\", IP %s\n", ssid_buf, ip4addr_ntoa(netif_ip4_addr(nif)));
}
