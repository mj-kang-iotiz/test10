#ifndef RS485_H
#define RS485_H

#include <stdio.h>

typedef struct {
  int (*init)(void);
  int (*start)(void);
  int (*stop)(void);
  int (*reset)(void);
  int (*send)(const char *data, size_t len);
  int (*recv)(char *buf, size_t len);
  void (*tx_enable)(void);
  void (*rx_enable)(void);
} rs485_hal_ops_t;

typedef struct
{
    const rs485_hal_ops_t *ops;
}rs485_t;

#endif
