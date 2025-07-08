#pragma once
#include <thread>
#include <sys/socket.h>
#include <semaphore>
#include <vector>
#include "TestLogger.hpp"

#define TEST_UART 2                // UART test code
#define TEST_SPI 4                 // SPI test code
#define TEST_I2C 8                 // I2C test code

#define N_ITERATIONS 1             // Default number of test iterations

class HardwareTester
{
public:
    HardwareTester();
    ~HardwareTester();

    bool connect();
    void runTests(uint8_t flags, uint8_t n_iter, std::string shared);
    std::string strLast();

private:
    void sendOutMsg();
    void recvInMsg(int idx);
    bool getNextTestId(uint32_t& id);
    void formatTimestamp(char* buffer, size_t size, const struct timeval& tv);
    double elapsedSeconds(const struct timeval& start, const struct timeval& end);

    int sock;
    sockaddr sockAddr;
    TestLogger* logger;

    struct OutMsg
    {
        uint32_t test_id;              /** Unique test ID */
        uint8_t peripheral;            /** Peripheral code */
        uint8_t n_iter;                /** Number of iterations */
        uint8_t p_len;                 /** Payload length */
        char payload[256];             /** Payload buffer */
    };

    struct InMsg
    {
        uint32_t test_id;              /** Unique test ID */
        uint8_t peripheral;            /** Peripheral code */
        uint8_t test_result;           /** Test result (success/fail) */
    };

    OutMsg outMsg;
    std::vector<InMsg> inMsgs;
    std::vector<int> results;
    std::vector<std::thread> threads;
    std::binary_semaphore doneSem{0};
};