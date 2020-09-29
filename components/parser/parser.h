#ifndef PARSER_H_
#define PARSER_H_
#include "battery.pb.h"
typedef struct _buffer_t *buffer_t;
typedef enum { MORE, DONE } parser_state_t;
int buffer_init(buffer_t buf);
void buffer_free(buffer_t buf);
parser_state_t lex(buffer_t *buf, battery_Stat *stat);
#endif
