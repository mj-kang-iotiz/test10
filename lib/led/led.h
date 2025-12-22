#ifndef LED_H
#define LED_H

#include "stm32f4xx.h"
#include "stm32f4xx_hal_gpio.h"
#include <stdbool.h>
#include <stdio.h>

typedef enum {
  LED_ID_NONE,
  LED_ID_1 = 1,
  LED_ID_2,
  LED_ID_3,
  LED_ID_MAX
} led_id_t;

typedef enum {
  LED_COLOR_NONE = 0,
  LED_COLOR_RED,
  LED_COLOR_YELLOW,
  LED_COLOR_GREEN,
  LED_COLOR_MAX
} led_color_t;

typedef struct {
  GPIO_TypeDef *r_port;
  GPIO_TypeDef *g_port;
  uint16_t r_pin;
  uint16_t g_pin;
  led_color_t color;
  bool toggle;
} led_port_t;

led_color_t led_get_color(led_id_t id);
void led_set_color(led_id_t id, led_color_t color);
void led_set_state(led_id_t id, bool on_off);
void led_set_toggle(led_id_t id);
void led_clear_all(void);
void led_init(void);

#endif
