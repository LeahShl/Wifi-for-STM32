#include "HardwareTester.hpp"
#define UUT_ADDR "192.168.1.177"   // IP address of Unit Under Test (UUT)
#define PORT 54321                 // Port for UDP communication
#define BUFSIZE 263                // Max possible size of OutMsg
#define IN_MSG_SIZE 6              // Incoming msg is always 6 bytes
#define N_TESTS 3                  // Total number of test types

#define TEST_SUCCESS 0x01          // Test success code
#define TEST_FAILED 0xff           // Test failed code

HardwareTester::HardwareTester()
{
}

HardwareTester::~HardwareTester()
{
}

bool HardwareTester::connect()
{
    return false;
}

void HardwareTester::runTests(uint8_t flags, uint8_t n_iter, std::string shared)
{
}

std::string HardwareTester::strLast()
{
    return std::string();
}

void HardwareTester::sendOutMsg()
{
}

void HardwareTester::recvInMsg(int idx)
{
}

bool HardwareTester::getNextTestId(uint32_t &id)
{
    return false;
}

void HardwareTester::formatTimestamp(char *buffer, size_t size, const timeval &tv)
{
}

double HardwareTester::elapsedSeconds(const timeval &start, const timeval &end)
{
    return 0.0;
}
