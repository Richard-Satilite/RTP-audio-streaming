#pragma once

#include <stddef.h>
#include <stdint.h>

#define AC_MAX_DESTINATIONS 16

typedef enum {
    AC_NET_OK = 0,
    AC_NET_ERR = -1,
    AC_NET_TIMEOUT = -2,
    AC_NET_ADDR_INVALID = -3,
    AC_NET_TOO_MANY_DESTINATIONS = -4
} ac_net_result_t;

typedef struct {
    char ip[64];
    uint16_t port;
} ac_net_endpoint_t;

typedef struct {
    intptr_t fd;
} ac_udp_socket_t;

typedef struct {
    ac_net_endpoint_t items[AC_MAX_DESTINATIONS];
    size_t count;
} ac_net_destination_list_t;

int ac_net_udp_open_sender(ac_udp_socket_t *sock);
int ac_net_udp_open_receiver(ac_udp_socket_t *sock, uint16_t listen_port);

void ac_net_udp_close(ac_udp_socket_t *sock);

int ac_net_udp_set_timeout(ac_udp_socket_t *sock, uint32_t timeout_ms);

int ac_net_destination_add(ac_net_destination_list_t *list,
                           const char *ip,
                           uint16_t port);

int ac_net_udp_send_to(ac_udp_socket_t *sock,
                       const ac_net_endpoint_t *dest,
                       const uint8_t *data,
                       size_t len);

int ac_net_udp_send_many(ac_udp_socket_t *sock,
                         const ac_net_destination_list_t *dests,
                         const uint8_t *data,
                         size_t len);

int ac_net_udp_recv(ac_udp_socket_t *sock,
                    uint8_t *buf,
                    size_t cap,
                    ac_net_endpoint_t *from);
