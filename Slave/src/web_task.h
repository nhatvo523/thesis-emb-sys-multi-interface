#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void initWebServer();
void webServerTask(void* pvParameters);