# Wifi For STM32
A wifi-to-uart bridge for embedded systems using ESP32.
## Overview
The system consists of three parts:
  1. [PlatformIO](https://github.com/platformio) project for ESP32 (this repo)
  2. PC software communicating over a udp socket (inside PC folder in this repo)
  3. STM32 Cube project communicating over uart (in [this](https://github.com/LeahShl/HW_TESTER_ESP32) repo)

Each part is designed to work as a highly independent unit: the STM32 communicating through uart not knwing about the bridge. The ESP bridge forwarding udp messages to uart and vice-versa, not knowing the content or length of the messages. The PC communicating over udp, not knowing abput the bridge.

I had to make a compromise on the uart-to-wifi part, since the uart communication is received as a series of bytes, not separated by packets. I chose to implement a start byte `0xAA` and end byte `0x55` to separate between packets. 
## PC Code
I provided two version of the PC code: C and C++. Both compile with `make` and have similar usage.

Almost the same as here: [FreeRTOS-HW-Verification](https://github.com/LeahShl/FreeRTOS-HW-Verification)
## Setup
1. Put your own wifi ssid and password in `include/config.h`
2. Build and upload to ESP32
3. Check what IP address you got. Put it in `PC/C/main.c` or `PC/CPP/HardwareTester.cpp` as the UUT address. Compile either one with `make`.
4. Connect ESP32's uart2 to STM32's usart2: `PIN_17 <-> PA3`, `PIN_16 <-> PD5`
5. Build and run STM32 Cube project
