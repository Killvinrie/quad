#include "main.h"
#include "control_app.h"
#include "motor_app.h"
#include "motor_logic.h"

/* TIM3 is clocked at 100 MHz (APB1 timer x2). Prescaler 99 gives a 1 MHz
 * counter. CH1..CH4 are PB4, PB5, PB0, PB1 on this board. */
static MotorLogic logic;
static volatile uint16_t requested_us = MOTOR_MIN_US;
static volatile uint32_t last_poll_ms;
static volatile uint8_t poll_stalled;

void MotorApp_Init(void)
{
    MotorLogic_Init(&logic);
    requested_us = MOTOR_MIN_US;
    last_poll_ms = HAL_GetTick();
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->CR1 = 0;
    TIM3->CCER = 0;
    TIM3->PSC = 99U;
    TIM3->ARR = 19999U;
    TIM3->CCR1 = MOTOR_MIN_US;
    TIM3->CCR2 = MOTOR_MIN_US;
    TIM3->CCR3 = MOTOR_MIN_US;
    TIM3->CCR4 = MOTOR_MIN_US;
    TIM3->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE |
                  (6U << 12) | TIM_CCMR1_OC2PE;
    TIM3->CCMR2 = (6U << 4) | TIM_CCMR2_OC3PE |
                  (6U << 12) | TIM_CCMR2_OC4PE;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E |
                 TIM_CCER_CC3E | TIM_CCER_CC4E;
    HAL_NVIC_SetPriority(TIM3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1 |= TIM_CR1_CEN;
}

void MotorApp_Poll(void)
{
    ControlCommand command;
    uint32_t now = HAL_GetTick();
    int valid = ControlApp_GetLatest(&command, now);
    if (poll_stalled) {
        MotorLogic_ForceDisarm(&logic);
        poll_stalled = 0;
    }
    requested_us = MotorLogic_Update(&logic, &command, valid);
    last_poll_ms = now;
    IWDG->KR = 0xAAAAU;
}

void MotorApp_StartWatchdog(void)
{
    /* LSI is nominally 32 kHz: /64 and reload 999 gives about 2 seconds.
     * Start only after sensor initialization, then feed from the main loop. */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;
    IWDG->RLR = 999U;
    while (IWDG->SR != 0U) {}
    IWDG->KR = 0xAAAAU;
}

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = 0;
        uint16_t pulse = requested_us;
        if ((uint32_t)(HAL_GetTick() - last_poll_ms) >
            CONTROL_LINK_TIMEOUT_MS) {
            pulse = MOTOR_MIN_US;
            poll_stalled = 1;
        }
        TIM3->CCR1 = pulse;
        TIM3->CCR2 = pulse;
        TIM3->CCR3 = pulse;
        TIM3->CCR4 = pulse;
    }
}
