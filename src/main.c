#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "driver/uart.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "config.h"

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *WIFI_TAG = "WIFI";
static const char *UART_TAG = "UART";

static int s_retry_num = 0;
#define MAX_RETRY 5

static struct sockaddr_in last_udp_sender;
static SemaphoreHandle_t sender_mutex;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(WIFI_TAG, "Wifi start: trying to connect...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(WIFI_TAG, "Retrying wifi connection (%d/%d)...", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(WIFI_TAG, "Failed to connect after %d retries", MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void init_wifi(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "Wifi init done. Waiting for IP...");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Successfully connected to SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(WIFI_TAG, "Connection to SSID: %s failed", WIFI_SSID);
    } else {
        ESP_LOGE(WIFI_TAG, "Unexpected wifi event");
    }
}

void init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(UART_TAG, "UART2 initialized on TX=%d RX=%d", UART_TX_PIN, UART_RX_PIN);
}

void ntouart_task(void *arg)
{
    char rx_buf[UDP_BUFFER_SIZE];
    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE("NTOUART", "Unable to create socket");
        vTaskDelete(NULL);
    }

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE("NTOUART", "Socket bind failed");
        close(sock);
        vTaskDelete(NULL);
    }

    ESP_LOGI("NTOUART", "Listening for UDP packets on port %d", UDP_PORT);

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                           (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE("NTOUART", "recvfrom failed");
            continue;
        }

        xSemaphoreTake(sender_mutex, portMAX_DELAY);
        memcpy(&last_udp_sender, &source_addr, sizeof(source_addr));
        xSemaphoreGive(sender_mutex);

        uart_write_bytes(UART_PORT_NUM, rx_buf, len);

        rx_buf[len] = '\0';
        ESP_LOGI("NTOUART", "Received %d bytes from %s:%d: %s",
                 len,
                 inet_ntoa(source_addr.sin_addr),
                 ntohs(source_addr.sin_port),
                 rx_buf);
    }

    close(sock);
    vTaskDelete(NULL);
}

void uartton_task(void *arg)
{
    uint8_t rx_buf[UART_BUF_SIZE];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE("UARTTON", "Failed to create socket");
        vTaskDelete(NULL);
    }

    ESP_LOGI("UARTTON", "Waiting for UART input...");

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(1000));

        if (len > 0) {
            struct sockaddr_in dest;
            xSemaphoreTake(sender_mutex, portMAX_DELAY);
            memcpy(&dest, &last_udp_sender, sizeof(dest));
            xSemaphoreGive(sender_mutex);

            rx_buf[len] = '\0';
            ESP_LOGI("UARTTON", "Received %d bytes from UART: %s", len, (char *)rx_buf);

            int sent = sendto(sock, rx_buf, len, 0, (struct sockaddr *)&dest, sizeof(dest));
            ESP_LOGI("UARTTON", "Forwarded %d bytes from UART to %s:%d",
                     sent, UDP_SOURCE_IP, UDP_PORT);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    init_wifi();
    init_uart();

    sender_mutex = xSemaphoreCreateMutex();
    if (sender_mutex == NULL) {
        ESP_LOGE("MAIN", "Failed to create sender_mutex!");
        abort();
    }

    xTaskCreate(ntouart_task, "ntouart_task", TASK_STACK_SIZE, NULL, TASK_PRIORITY_NTOU, NULL);
    xTaskCreate(uartton_task, "uartton_task", TASK_STACK_SIZE, NULL, TASK_PRIORITY_UART, NULL);
}
