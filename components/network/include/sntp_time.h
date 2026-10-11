#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sntp_time_init(void);
bool sntp_time_is_reliable(void);
