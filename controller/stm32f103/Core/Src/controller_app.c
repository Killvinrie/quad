#include "controller_app.h"
#include "main.h"
#include "nrf24l01.h"
#include "../../../../software/Common/control_link.h"

extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;

static Nrf24 radio;
static uint8_t sequence;
static uint16_t button_raw, button_stable;
static uint8_t button_same_count;

static uint16_t read_axis(uint32_t channel)
{
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK ||
        HAL_ADC_Start(&hadc1) != HAL_OK ||
        HAL_ADC_PollForConversion(&hadc1, 5) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return 2048;
    }
    uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);
    return value;
}

static uint16_t read_buttons_raw(void)
{
    uint16_t buttons = 0;
    if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET) buttons |= 1U << 0;
    if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET) buttons |= 1U << 1;
    if (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET) buttons |= 1U << 2;
    if (HAL_GPIO_ReadPin(KEY4_GPIO_Port, KEY4_Pin) == GPIO_PIN_RESET) buttons |= 1U << 3;
    if (HAL_GPIO_ReadPin(KEY5_GPIO_Port, KEY5_Pin) == GPIO_PIN_RESET) buttons |= 1U << 4;
    if (HAL_GPIO_ReadPin(KEY6_GPIO_Port, KEY6_Pin) == GPIO_PIN_RESET) buttons |= 1U << 5;
    if (HAL_GPIO_ReadPin(KEY7_GPIO_Port, KEY7_Pin) == GPIO_PIN_RESET) buttons |= 1U << 6;
    if (HAL_GPIO_ReadPin(KEY8_GPIO_Port, KEY8_Pin) == GPIO_PIN_RESET) buttons |= 1U << 7;
    return buttons;
}

static uint16_t read_buttons(void)
{
    uint16_t raw = read_buttons_raw();
    if (raw == button_raw) {
        if (button_same_count < 3U) ++button_same_count;
    } else {
        button_raw = raw;
        button_same_count = 0;
    }
    /* Two consecutive 20ms samples provide a simple 40ms debounce. */
    if (button_same_count >= 2U) button_stable = button_raw;
    return button_stable;
}

void ControllerApp_Init(void)
{
    radio.spi = &hspi2;
    radio.ce_port = CE_GPIO_Port; radio.ce_pin = CE_Pin;
    radio.csn_port = CSN_GPIO_Port; radio.csn_pin = CSN_Pin;
    (void)Nrf24_TxInit(&radio);
}

void ControllerApp_Poll(void)
{
    static uint32_t tick;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - tick) < 20U) return;
    tick = now;

    ControlCommand command = {0};
    command.sequence = sequence++;
    command.axis[0] = read_axis(ADC_CHANNEL_0); /* YAW */
    command.axis[1] = read_axis(ADC_CHANNEL_1); /* THR */
    command.axis[2] = read_axis(ADC_CHANNEL_2); /* PITCH */
    command.axis[3] = read_axis(ADC_CHANNEL_3); /* ROLL */
    command.buttons = read_buttons();
    command.flags = 0;
    command.battery_mv = 0; /* No VBAT ADC channel exists in this schematic. */

    uint8_t frame[CONTROL_FRAME_SIZE];
    control_encode(frame, &command);
    /* UART1 is an optional wired ESP32 path; radio is the normal path. */
    (void)HAL_UART_Transmit(&huart1, frame, sizeof(frame), 5);
    (void)Nrf24_Send(&radio, frame, sizeof(frame));
}
