/* Copyright (C) StrawberryHacker */

#ifndef DYNAMIC_LINKER_H
#define DYNAMIC_LINKER_H

#include "types.h"
#include "scheduler.h"

tid_t dynamic_linker_run(u32* binary);

#endif