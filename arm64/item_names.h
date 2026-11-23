#pragma once
#include <stddef.h>
/* Own the original first, length-prefixed SYSMES.ALD bank. */
const char *rx_item_names_load(const void*,size_t);
const char *rx_item_names_span(unsigned,const unsigned char**,size_t*);
void rx_item_names_clear(void);
/* Separately owned second SYSMES bank; opcode support is checked by readers. */
const char *rx_system_messages_load(const void*,size_t);
const char *rx_system_messages_data(const unsigned char**,size_t*,unsigned*);
int rx_system_message_current(void);
