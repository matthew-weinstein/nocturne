#pragma once

#include <stdbool.h>
#include <stddef.h>

#define BACKPRESSURE_ENGAGE_PCT  60
#define BACKPRESSURE_RELEASE_PCT 40

bool backpressure_update(bool engaged, size_t occupancy, size_t capacity);
