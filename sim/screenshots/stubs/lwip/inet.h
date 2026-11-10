#pragma once
#include <arpa/inet.h>
#include <cstdio>
#define IP4ADDR_STRLEN_MAX 16
typedef struct { uint32_t addr; } ip4_addr_t;
inline char *ip4addr_ntoa_r(const ip4_addr_t *addr, char *buffer, int length)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(&addr->addr);
    std::snprintf(buffer, length, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return buffer;
}
