#include "network_identity.h"

#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lwip/pbuf.h"
#include "lwip/prot/dhcp.h"
#include "panel_lwip_hooks.h"

#define DOMAIN_NAME_OPTION 15

static portMUX_TYPE s_domain_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_domain[PANEL_DOMAIN_MAX + 1];

void network_identity_clear_domain(void)
{
    portENTER_CRITICAL(&s_domain_lock);
    s_domain[0] = '\0';
    portEXIT_CRITICAL(&s_domain_lock);
}

void network_identity_get_domain(char *dst, size_t size)
{
    portENTER_CRITICAL(&s_domain_lock);
    strlcpy(dst, s_domain, size);
    portEXIT_CRITICAL(&s_domain_lock);
}

void panel_dhcp_request_domain(struct dhcp_msg *msg, u16_t *length)
{
    /* Insert into the existing parameter request list, never emit a duplicate
     * option 55. The same hook runs for discovery, selection and lease renewal. */
    size_t used = *length;
    if (used >= DHCP_OPTIONS_LEN - 1) return; /* retain space for END */
    for (size_t i = 0; i < used;) {
        unsigned option = msg->options[i];
        if (option == DHCP_OPTION_END) break;
        if (option == DHCP_OPTION_PAD) { ++i; continue; }
        if (i + 1 >= used) return;
        size_t len = msg->options[i + 1];
        size_t next = i + 2 + len;
        if (next > used) return;
        if (option == DHCP_OPTION_PARAMETER_REQUEST_LIST) {
            if (memchr(msg->options + i + 2, DOMAIN_NAME_OPTION, len) || len == 255) return;
            memmove(msg->options + next + 1, msg->options + next, used - next);
            msg->options[next] = DOMAIN_NAME_OPTION;
            msg->options[i + 1]++;
            ++*length;
            return;
        }
        i = next;
    }
}

static bool normalize_domain(char *domain, size_t length)
{
    if (length && domain[length - 1] == '.') domain[--length] = '\0';
    if (!length || length > PANEL_DOMAIN_MAX) return false;
    size_t label = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)domain[i];
        if (c >= 'A' && c <= 'Z') domain[i] = c = c + ('a' - 'A');
        if (c == '.') {
            if (!label || domain[i - 1] == '-') return false;
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
                (!label && c == '-') || ++label > 63) return false;
        }
    }
    return label && domain[length - 1] != '-';
}

void panel_dhcp_parse_domain(u8_t message_type, u8_t option, u8_t length,
                             struct pbuf *p, u16_t offset)
{
    /* RFC 2131 requires message type to be the first option. Only learn from
     * ACKs (not offers); copy via pbuf API because options can cross buffers. */
    if (message_type != DHCP_ACK || option != DOMAIN_NAME_OPTION) return;
    char domain[PANEL_DOMAIN_MAX + 2] = {0};
    if (length > PANEL_DOMAIN_MAX + 1 ||
        pbuf_copy_partial(p, domain, length, offset) != length ||
        !normalize_domain(domain, length)) {
        network_identity_clear_domain();
        return;
    }
    portENTER_CRITICAL(&s_domain_lock);
    strlcpy(s_domain, domain, sizeof(s_domain));
    portEXIT_CRITICAL(&s_domain_lock);
}
