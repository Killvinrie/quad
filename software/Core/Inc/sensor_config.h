#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H
/* PCB 2026-09-04: MPU PB6/PB7, BMP PB10/PB3, GPS PA9/PA10.
 * STM32 PA2 TX -> ESP32 GPIO18 RX; PA3 RX <- ESP32 GPIO17 TX. */
#define SENSOR_GPS_BAUD 9600U
#define SENSOR_LINK_BAUD 115200U
#define SENSOR_I2C_HZ 100000U
#define SENSOR_IO_TIMEOUT_MS 20U
#define SENSOR_RETRY_MS 2000U
/* ADC input is 10/43 of source voltage; software reports reconstructed source. */
#define SENSOR_ADC_VREF_MV 3300U
/* Battery divider: ADC input is 10/43 of the source voltage. */
#define SENSOR_DIVIDER_NUMERATOR 10U
#define SENSOR_DIVIDER_DENOMINATOR 43U
/* Sensor smoothing: EMA shift 3 = 1/8 new sample, startup pressure baseline. */
#define SENSOR_BARO_FILTER_SHIFT 3U
#define SENSOR_BARO_BASELINE_SAMPLES 32U
#define SENSOR_IMU_FILTER_SHIFT 2U
#define SENSOR_ADC_FILTER_SHIFT 2U
#endif
