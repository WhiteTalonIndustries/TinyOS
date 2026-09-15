#ifndef TINYOS_NET_H
#define TINYOS_NET_H

/* `ping`/`browser` shell command handlers, built directly on wifi.c's
 * lwIP stack (NO_SYS/polling). Both are no-ops (print an error) if WiFi
 * isn't connected -- run `wifi connect` first. */

void net_ping(const char *host);

/* "Super limited browser": plain HTTP/1.0 GET, no HTTPS, no redirects,
 * no HTML rendering -- dumps the raw response (headers and all) to the
 * console. path may be NULL for "/". */
void net_get(const char *host, const char *path);

#endif
