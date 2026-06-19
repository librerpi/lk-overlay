#pragma once
void spi_init(void);
void spi_begin(void);
void spi_end(void);
void spi_flash_read_data(uint8_t *buffer, uint32_t offset, size_t length);
