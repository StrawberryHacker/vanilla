/* Copyright (C) StrawberryHacker */

#ifndef GPIO_H
#define GPIO_H

#include "types.h"
#include "hardware.h"

enum gpio_func {
    GPIO_FUNC_A,
    GPIO_FUNC_B,
    GPIO_FUNC_C,
    GPIO_FUNC_D,
    GPIO_FUNC_OFF
};

enum gpio_dir {
    GPIO_INPUT,
    GPIO_OUTPUT
};

enum gpio_pull {
    GPIO_PULL_UP,
    GPIO_PULL_DOWN
};

enum gpio_irq_src {
    GPIO_RISING_EDGE,
    GPIO_FALLING_EDGE,
    GPIO_HIGH_LEVEL,
    GPIO_LOW_LEVEL,
    GPIO_EDGE,
};

enum gpio_filter {
    GPIO_GLITCH_FILTER,
    GPIO_DEBOUNCE_FILTER
};

static inline void gpio_set(gpio_reg* port, u8 pin) {
    port->SODR = (1 << pin);
}

static inline void gpio_clear(gpio_reg* port, u8 pin) {
    port->CODR = (1 << pin);
}

void gpio_set_function(gpio_reg* port, u8 pin, enum gpio_func func);

void gpio_set_direction(gpio_reg* port, u8 pin, enum gpio_dir dir);

void gpio_interrupt_enable(gpio_reg* port, u8 pin, enum gpio_irq_src src);

void gpio_toggle(gpio_reg* port, u8 pin);

u32 gpio_read(gpio_reg* port);

u8 gpio_get_pin_status(gpio_reg* port, u8 pin);

void gpio_set_pull(gpio_reg* port, u8 pin, enum gpio_pull pull);

u32 gpio_get_interrupt_status(gpio_reg* port);

void gpio_set_filter(gpio_reg* port, u8 pin, enum gpio_filter filt, u32 us);

#endif
