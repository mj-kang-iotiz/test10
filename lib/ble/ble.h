#ifndef BLE_H
#define BLE_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
  int (*init)(void);
  int (*start)(void);
  int (*stop)(void);
  int (*reset)(void);
  int (*send)(const char *data, size_t len);
  int (*recv)(char *buf, size_t len);
  int (*at_mode)(void);
  int (*bypass_mode)(void);
} ble_hal_ops_t;

typedef struct
{
    const ble_hal_ops_t *ops;
    bool rdy;
}ble_t;

#endif
