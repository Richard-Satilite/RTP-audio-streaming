#ifdef _WIN32

#include "ac/net.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define AC_NET_INVALID_SOCKET ((intptr_t)INVALID_SOCKET)

static int ac_net_wsa_startup(void)
{
    static int initialized = 0;
    WSADATA data;

    if (initialized) {
        return AC_NET_OK;
    }

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return AC_NET_ERR;
    }

    initialized = 1;
    return AC_NET_OK;
}

static int ac_net_endpoint_to_addr(const ac_net_endpoint_t *endpoint,
                                   struct sockaddr_in *addr)
{
    if (endpoint == NULL || addr == NULL || endpoint->port == 0) {
        return AC_NET_ADDR_INVALID;
    }

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(endpoint->port);

    if (InetPtonA(AF_INET, endpoint->ip, &addr->sin_addr) != 1) {
        return AC_NET_ADDR_INVALID;
    }

    return AC_NET_OK;
}

int ac_net_udp_open_sender(ac_udp_socket_t *sock)
{
    SOCKET s;

    if (sock == NULL) {
        return AC_NET_ERR;
    }

    if (ac_net_wsa_startup() != AC_NET_OK) {
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    sock->fd = (intptr_t)s;
    return AC_NET_OK;
}

int ac_net_udp_open_receiver(ac_udp_socket_t *sock, uint16_t listen_port)
{
    SOCKET s;
    BOOL reuse = TRUE;
    struct sockaddr_in addr;

    if (sock == NULL || listen_port == 0) {
        return AC_NET_ERR;
    }

    if (ac_net_wsa_startup() != AC_NET_OK) {
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) != 0) {
        closesocket(s);
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port);

    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        sock->fd = AC_NET_INVALID_SOCKET;
        return AC_NET_ERR;
    }

    sock->fd = (intptr_t)s;
    return AC_NET_OK;
}

int ac_net_udp_set_timeout(ac_udp_socket_t *sock, uint32_t timeout_ms)
{
    DWORD timeout;
    if (sock == NULL || sock->fd == AC_NET_INVALID_SOCKET) return AC_NET_ERR;
    timeout = (DWORD)timeout_ms;
    return setsockopt((SOCKET)sock->fd, SOL_SOCKET, SO_RCVTIMEO,
                      (const char *)&timeout, sizeof(timeout)) == 0 ? AC_NET_OK : AC_NET_ERR;
}

void ac_net_udp_close(ac_udp_socket_t *sock)
{
    if (sock == NULL || sock->fd == AC_NET_INVALID_SOCKET) {
        return;
    }

    closesocket((SOCKET)sock->fd);
    sock->fd = AC_NET_INVALID_SOCKET;
}

int ac_net_destination_add(ac_net_destination_list_t *list,
                           const char *ip,
                           uint16_t port)
{
    ac_net_endpoint_t *endpoint;
    struct sockaddr_in unused;

    if (list == NULL || ip == NULL || port == 0) {
        return AC_NET_ADDR_INVALID;
    }

    if (list->count >= AC_MAX_DESTINATIONS) {
        return AC_NET_TOO_MANY_DESTINATIONS;
    }

    endpoint = &list->items[list->count];

    if (snprintf(endpoint->ip, sizeof(endpoint->ip), "%s", ip) >=
        (int)sizeof(endpoint->ip)) {
        endpoint->ip[0] = '\0';
        return AC_NET_ADDR_INVALID;
    }

    endpoint->port = port;

    if (ac_net_endpoint_to_addr(endpoint, &unused) != AC_NET_OK) {
        endpoint->ip[0] = '\0';
        endpoint->port = 0;
        return AC_NET_ADDR_INVALID;
    }

    list->count++;
    return AC_NET_OK;
}

int ac_net_udp_send_to(ac_udp_socket_t *sock,
                       const ac_net_endpoint_t *dest,
                       const uint8_t *data,
                       size_t len)
{
    int sent;
    struct sockaddr_in addr;

    if (sock == NULL || sock->fd == AC_NET_INVALID_SOCKET || data == NULL || len == 0) {
        return AC_NET_ERR;
    }

    if (len > INT_MAX) {
        return AC_NET_ERR;
    }

    if (ac_net_endpoint_to_addr(dest, &addr) != AC_NET_OK) {
        return AC_NET_ADDR_INVALID;
    }

    sent = sendto((SOCKET)sock->fd,
                  (const char *)data,
                  (int)len,
                  0,
                  (const struct sockaddr *)&addr,
                  sizeof(addr));
    if (sent == SOCKET_ERROR || (size_t)sent != len) {
        return AC_NET_ERR;
    }

    return AC_NET_OK;
}

int ac_net_udp_send_many(ac_udp_socket_t *sock,
                         const ac_net_destination_list_t *dests,
                         const uint8_t *data,
                         size_t len)
{
    size_t i;

    if (dests == NULL || dests->count == 0) {
        return AC_NET_ERR;
    }

    for (i = 0; i < dests->count; ++i) {
        int result = ac_net_udp_send_to(sock, &dests->items[i], data, len);
        if (result != AC_NET_OK) {
            return result;
        }
    }

    return AC_NET_OK;
}

int ac_net_udp_recv(ac_udp_socket_t *sock,
                    uint8_t *buf,
                    size_t cap,
                    ac_net_endpoint_t *from)
{
    int received;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    if (sock == NULL || sock->fd == AC_NET_INVALID_SOCKET || buf == NULL || cap == 0) {
        return AC_NET_ERR;
    }

    if (cap > INT_MAX) {
        return AC_NET_ERR;
    }

    received = recvfrom((SOCKET)sock->fd,
                        (char *)buf,
                        (int)cap,
                        0,
                        (struct sockaddr *)&addr,
                        &addr_len);
    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
            return AC_NET_TIMEOUT;
        }
        return AC_NET_ERR;
    }

    if (from != NULL) {
        const char *ip = InetNtopA(AF_INET,
                                   &addr.sin_addr,
                                   from->ip,
                                   (DWORD)sizeof(from->ip));
        if (ip == NULL) {
            from->ip[0] = '\0';
            from->port = 0;
            return AC_NET_ERR;
        }
        from->port = ntohs(addr.sin_port);
    }

    return received;
}

#endif