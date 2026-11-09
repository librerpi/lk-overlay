#pragma once

#include <stddef.h>

/* Send a generic DSI write packet to the panel bridge. */
int bcm_dsi1_generic_write(const void *data, size_t len);
