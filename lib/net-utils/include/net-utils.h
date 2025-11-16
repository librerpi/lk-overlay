#pragma once

ssize_t tftp_blocking_get(ip_addr_t hostip, const char *path, uint32_t size, uint8_t *buffer);
