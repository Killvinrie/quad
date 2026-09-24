#ifndef QUAD_PHONE_SERVER_H
#define QUAD_PHONE_SERVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

/* Starts the controller-only SoftAP and HTTP page. A one-slot queue holds the
 * latest valid phone command so old network requests cannot build a backlog. */
esp_err_t PhoneServer_Start(QueueHandle_t command_queue);

#endif
