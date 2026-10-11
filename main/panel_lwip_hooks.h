#pragma once

#include "lwip/arch.h"

struct netif;
struct dhcp_msg;
struct pbuf;

void panel_dhcp_request_domain(struct dhcp_msg *msg, u16_t *length);
void panel_dhcp_parse_domain(u8_t message_type, u8_t option, u8_t length,
                             struct pbuf *p, u16_t offset);

/* lwipopts.h supplies the IDF defaults before including this hook file. */
#undef LWIP_HOOK_DHCP_APPEND_OPTIONS
#define LWIP_HOOK_DHCP_APPEND_OPTIONS(netif, dhcp, state, msg, type, length) do { \
    dhcp_append_extra_opts(netif, state, msg, length); \
    panel_dhcp_request_domain(msg, length); \
} while (0)

#undef LWIP_HOOK_DHCP_PARSE_OPTION
#define LWIP_HOOK_DHCP_PARSE_OPTION(netif, dhcp, state, msg, type, option, len, p, offset) do { \
    LWIP_UNUSED_ARG(msg); \
    dhcp_parse_extra_opts(dhcp, state, option, len, p, offset); \
    panel_dhcp_parse_domain(type, option, len, p, offset); \
} while (0)
