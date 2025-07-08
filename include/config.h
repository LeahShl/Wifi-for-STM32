#pragma once

#define WIFI_SSID          ""
#define WIFI_PASSWORD      ""

#define UART_PORT_NUM      UART_NUM_2
#define UART_BAUD_RATE     115200
#define UART_TX_PIN        17
#define UART_RX_PIN        16
#define UART_BUF_SIZE      1024

#define START_BYTE         0xAA
#define END_BYTE           0x55

#define UDP_SOURCE_IP       "192.168.1.71"
#define UDP_PORT            54321
#define UDP_BUFFER_SIZE     1024

#define TASK_STACK_SIZE     4096
#define TASK_PRIORITY_NTOU  10
#define TASK_PRIORITY_UART  9