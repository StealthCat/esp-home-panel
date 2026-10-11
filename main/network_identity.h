#pragma once

#include <stddef.h>

/* DNS names are at most 253 printable bytes, excluding a trailing root dot. */
#define PANEL_DOMAIN_MAX 253

void network_identity_clear_domain(void);
void network_identity_get_domain(char *dst, size_t size);
