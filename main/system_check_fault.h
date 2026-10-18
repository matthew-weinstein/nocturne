#pragma once

#include <stdbool.h>

void system_check_fault_before_start(void);
void system_check_fault_arm(void);
void system_check_fault_before_stop(void);
bool system_check_fault_injected(void);
