/*
 * Copyright (c) 2022, xiaofan <xfan1024@live.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __socket_util_h__
#define __socket_util_h__

#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netfilter_ipv4.h>
#include <boost/asio/ip/tcp.hpp>

static inline bool sockaddr_to_endpoint(struct sockaddr *addr, boost::asio::ip::tcp::endpoint *ep)
{
    boost::asio::ip::address ipaddr;
    uint16_t port;
    switch (addr->sa_family)
    {
        case AF_INET: {
            struct sockaddr_in *addr4 = (struct sockaddr_in*)addr;
            port = ntohs(addr4->sin_port);
            ipaddr = boost::asio::ip::address_v4(ntohl(addr4->sin_addr.s_addr));
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6*)addr;
            port = ntohs(addr6->sin6_port);
            auto p_addr_bytes = reinterpret_cast<boost::asio::ip::address_v6::bytes_type*>(&addr6->sin6_addr);
            ipaddr = boost::asio::ip::address_v6(*p_addr_bytes, addr6->sin6_scope_id);
            break;
        }
        default:
            return false;
    }
    *ep = boost::asio::ip::tcp::endpoint(ipaddr, port);
    return true;
}

static inline bool get_original_destination(int fd, boost::asio::ip::tcp::endpoint *ep)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    int res = getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, (struct sockaddr *)&addr, &len);
    if (res < 0)
        return false;
    bool result = sockaddr_to_endpoint((struct sockaddr*)&addr, ep);
    if (!result)
        return false;
    return true;
}

#endif
