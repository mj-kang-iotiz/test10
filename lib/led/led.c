#include "led.h"
#include <string.h>

#ifndef TAG
  #define TAG "LED"
#endif

#include "log.h"

#define LED1_R_PORT GPIOC
#define LED1_R_PIN GPIO_PIN_1
#define LED1_G_PORT GPIOC
#define LED1_G_PIN GPIO_PIN_2

#define LED2_R_PORT GPIOC
#define LED2_R_PIN GPIO_PIN_3
#define LED2_G_PORT GPIOC
#define LED2_G_PIN GPIO_PIN_4

#define LED3_R_PORT GPIOB
#define LED3_R_PIN GPIO_PIN_13
#define LED3_G_PORT GPIOB
#define LED3_G_PIN GPIO_PIN_14

static led_port_t led_handle[3];

void led_set_color(led_id_t id, led_color_t color) {
  if (id < LED_ID_MAX && id != LED_ID_NONE) {
    if (color < LED_COLOR_MAX) {
      led_handle[id - 1].color = color;
    }
  }
}

led_color_t led_get_color(led_id_t id)
{
  return led_handle[id - 1].color;
}

void _set_color(led_port_t *port, bool on_off) {
  if (on_off) {
    switch (port->color) {
    case LED_COLOR_NONE:
      HAL_GPIO_WritePin(port->r_port, port->r_pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(port->g_port, port->g_pin, GPIO_PIN_SET);
      break;

    case LED_COLOR_RED:
      HAL_GPIO_WritePin(port->r_port, port->r_pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(port->g_port, port->g_pin, GPIO_PIN_SET);
      break;

    case LED_COLOR_YELLOW:
      HAL_GPIO_WritePin(port->r_port, port->r_pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(port->g_port, port->g_pin, GPIO_PIN_RESET);
      break;

    case LED_COLOR_GREEN:
      HAL_GPIO_WritePin(port->r_port, port->r_pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(port->g_port, port->g_pin, GPIO_PIN_RESET);
      break;

    default:
      LOG_ERR("INVALID LED COLOR : %d", port->color);
    }
  } else {
    HAL_GPIO_WritePin(port->r_port, port->r_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(port->g_port, port->g_pin, GPIO_PIN_SET);
  }
}

void led_set_state(led_id_t id, bool on_off) {
  if (id < LED_ID_MAX && id != LED_ID_NONE) {
    led_port_t *port = &led_handle[id - 1];
    _set_color(port, on_off);
  }
}

void led_set_toggle(led_id_t id) {
  if (id < LED_ID_MAX && id != LED_ID_NONE) {
    led_port_t *handle = &led_handle[id - 1];
    if (handle->color != LED_COLOR_NONE && handle->color != LED_COLOR_MAX) {
      if (!handle->toggle) {
        led_set_state(id, true);
      } else {
        led_set_state(id, false);
      }

      handle->toggle = !handle->toggle;
    }
  }
}

void led_clear_all(void) {
  for (int i = LED_ID_1; i < LED_ID_MAX; i++) {
    led_set_color(i, LED_COLOR_NONE);
    led_set_state(i, false);
  }
}

void led_init(void) {
  memset(led_handle, 0, sizeof(led_handle));

  led_handle[0].r_port = LED1_R_PORT;
  led_handle[0].g_port = LED1_G_PORT;
  led_handle[0].r_pin = LED1_R_PIN;
  led_handle[0].g_pin = LED1_G_PIN;

  led_handle[1].r_port = LED2_R_PORT;
  led_handle[1].g_port = LED2_G_PORT;
  led_handle[1].r_pin = LED2_R_PIN;
  led_handle[1].g_pin = LED2_G_PIN;

  led_handle[2].r_port = LED3_R_PORT;
  led_handle[2].g_port = LED3_G_PORT;
  led_handle[2].r_pin = LED3_R_PIN;
  led_handle[2].g_pin = LED3_G_PIN;

  led_clear_all();
}
