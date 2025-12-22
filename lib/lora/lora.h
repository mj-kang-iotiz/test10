#ifndef LORA_H
#define LORA_H

#include <stdio.h>

typedef struct {
  int (*init)(void);
  int (*start)(void);
  int (*stop)(void);
  int (*reset)(void);
  int (*send)(const char *data, size_t len);
  int (*recv)(char *buf, size_t len);
} lora_hal_ops_t;

typedef struct
{
	const lora_hal_ops_t *ops;
}lora_t;

void lora_init(lora_t* handle);


#endif
