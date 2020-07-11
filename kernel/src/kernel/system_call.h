/// Copyright (C) StrawberryHacker

#ifndef SYSTEM_CALL_H
#define SYSTEM_CALL_H

#include "types.h"
#include "hardware.h"
#include "mm.h"
#include "cpu.h"

#define NAKED __attribute__((naked))

void NOINLINE syscall_thread_sleep(u32 ms);

void NAKED NOINLINE syscall_gpio_toggle(gpio_reg* port, u8 pin);

void* NAKED NOINLINE syscall_mm_alloc(u32 size, enum physmem_e region);

void NAKED NOINLINE syscall_mm_free(void* ptr);

#endif