#include "control_app.h"
#include "main.h"

static ControlParser parser;
static volatile ControlCommand latest;
static volatile uint32_t latest_tick;
static volatile uint8_t latest_valid;

void ControlApp_Init(void)
{
    parser.used = 0;
    latest_valid = 0;
    latest_tick = 0;
}

void ControlApp_RxByte(uint8_t byte)
{
    uint8_t frame[CONTROL_FRAME_SIZE];
    ControlCommand command;
    if (!control_feed(&parser, byte, frame) || !control_decode(frame, &command))
        return;
    /* The USART2 ISR is the only writer. Keep the main-loop snapshot atomic. */
    latest = command;
    latest_tick = HAL_GetTick();
    latest_valid = 1;
}

int ControlApp_GetLatest(ControlCommand *command, uint32_t now)
{
    uint32_t tick;
    uint8_t valid;
    __disable_irq();
    *command = latest;
    tick = latest_tick;
    valid = latest_valid;
    __enable_irq();
    return valid && (uint32_t)(now - tick) <= CONTROL_LINK_TIMEOUT_MS;
}
