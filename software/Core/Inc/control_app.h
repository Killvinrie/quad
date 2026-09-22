#ifndef CONTROL_APP_H
#define CONTROL_APP_H

#include <stdint.h>
#include "control_link.h"

#define CONTROL_LINK_TIMEOUT_MS 250U

void ControlApp_Init(void);
void ControlApp_RxByte(uint8_t byte);
int ControlApp_GetLatest(ControlCommand *command, uint32_t now);

#endif
