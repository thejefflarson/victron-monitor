#include "battery.pb.h"
#include "esp_err.h"
#include <stddef.h>

/*!max:re2c*/
#ifndef YYMAXFILL
#define YYMAXFILL 0
#error This file needs to be preprocessed with re2c
#endif

typedef struct {
  uint8_t *data;
  size_t length;
} _buffer_t;

int buffer_init(_buffer_t *buf) {
  buf->data = calloc(1 + YYMAXFILL, sizeof(uint8_t));
  buf->length = 0;
  if (buf->data == NULL) {
    return ESP_ERR_NO_MEM;
  } else {
    return ESP_OK;
  }
}

void buffer_free(_buffer_t *buf) { free(buf->data); }

typedef enum { MORE, DONE } parser_state_t;
// https://re2c.org/manual/manual_c.html#storable-state
parser_state_t lex(_buffer_t *buf, battery_Stat *stat) { return DONE; }
