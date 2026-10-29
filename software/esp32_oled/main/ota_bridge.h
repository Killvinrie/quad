#pragma once
#include <stdbool.h>
#include <stdint.h>
void OtaBridge_Start(void);
void OtaBridge_Rx(uint8_t byte);
bool OtaBridge_Busy(void);
