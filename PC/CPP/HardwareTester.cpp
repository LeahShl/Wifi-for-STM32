#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <semaphore>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include "HardwareTester.hpp"


#define UUT_ADDR "192.168.1.45"    // IP address of Unit Under Test (UUT)
#define PORT 54321                 // Port for UDP communication
#define BUFSIZE 263                // Max possible size of OutMsg
#define IN_MSG_SIZE 6              // Incoming msg is always 6 bytes
#define N_TESTS 3                  // Total number of test types

#define TEST_SUCCESS 0x01          // Test success code
#define TEST_FAILED 0xff           // Test failed code

/**
 * @brief Construct a new Hardware Tester:: Hardware Tester object
 * 
 */
HardwareTester::HardwareTester() : sock(-1), logger(new TestLogger())
{}

/**
 * @brief Destroy the Hardware Tester:: Hardware Tester object
 * 
 */
HardwareTester::~HardwareTester()
{
    if (sock != -1) close(sock);
    delete logger;
}


/**
 * @brief Initializes and connects the UDP socket to the UUT (Unit Under Test).
 *
 * Resolves the UUT address and sets up the sockaddr_in structure.
 *
 * @return true if the connection setup succeeds.
 * @return false if socket creation or host resolution fails.
 */
bool HardwareTester::connect()
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return false;
    }

    struct hostent *host = gethostbyname(UUT_ADDR);
    if (!host)
    {
        perror("gethostbyname");
        return false;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)&sockAddr;
    addr_in->sin_family = AF_INET;
    addr_in->sin_port = htons(PORT);
    addr_in->sin_addr = *((struct in_addr *)host->h_addr);
    memset(&(addr_in->sin_zero), 0, 8);

    return true;
}

/**
 * @brief Runs a group of hardware tests based on the selected flags.
 *
 * Prepares and sends an OutMsg to the UUT and waits for corresponding responses.
 * Each peripheral test runs in a separate thread.
 *
 * @param flags Bitmask indicating which tests to run (UART, SPI, I2C).
 * @param n_iter Number of iterations each test should run.
 * @param shared Optional shared payload to send with the test request.
 */
void HardwareTester::runTests(uint8_t flags, uint8_t n_iter, std::string shared)
{
    inMsgs.clear();
    results.clear();
    threads.clear();

    outMsg.p_len = shared.length();
    outMsg.n_iter = n_iter;
    outMsg.peripheral = flags;
    std::strncpy(outMsg.payload, shared.c_str(), sizeof(outMsg.payload));

    uint32_t test_id;
    if (!getNextTestId(test_id))
    {
        std::cerr << "Error getting test id\n";
        return;
    }
    outMsg.test_id = test_id;

    struct timeval start, end;
    gettimeofday(&start, nullptr);

    sendOutMsg();

    int n_threads = 0;
    if (flags & TEST_UART) ++n_threads;
    if (flags & TEST_SPI) ++n_threads;
    if (flags & TEST_I2C) ++n_threads;

    inMsgs.resize(n_threads);
    results.resize(n_threads);

    for (int i = 0; i < n_threads; ++i) {
        threads.emplace_back(&HardwareTester::recvInMsg, this, i);
    }

    for (auto& t : threads) t.join();

    gettimeofday(&end, nullptr);

    char timestamp[64];
    formatTimestamp(timestamp, sizeof(timestamp), start);

    int all_success = 1;
    for (int r : results) if (!r) all_success = 0;

    try {
        logger->prep();
        logger->logTest(test_id, timestamp, elapsedSeconds(start, end), all_success);
    } 
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return;
    }
}

/**
 * @brief Returns a string representation of the last test result.
 *
 * Queries the logger for the test result using the last test ID.
 *
 * @return std::string Description of the last test result or an error message.
 */
std::string HardwareTester::strLast()
{
    try
    {
        logger->prep();
        return logger->strById(outMsg.test_id);
    } 
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
    }

    return std::string("Error getting last test's result");
}


/**
 * @brief Sends the prepared OutMsg structure over UDP.
 *
 * Serializes the `outMsg` into a byte buffer and sends it to the UUT.
 *
 * @throws std::runtime_error if `sendto` fails.
 */
void HardwareTester::sendOutMsg()
{
    char buf[BUFSIZE];
    size_t n_bytes = 0;

    std::memcpy(&buf[n_bytes], &outMsg.test_id, sizeof(outMsg.test_id));
    n_bytes += sizeof(outMsg.test_id);

    buf[n_bytes++] = outMsg.peripheral;
    buf[n_bytes++] = outMsg.n_iter;
    buf[n_bytes++] = outMsg.p_len;

    if (outMsg.p_len > 0) {
        std::memcpy(&buf[n_bytes], outMsg.payload, outMsg.p_len);
        n_bytes += outMsg.p_len;
    }

    if (sendto(sock, buf, n_bytes, 0, (struct sockaddr *)&sockAddr, sizeof(sockAddr)) != (ssize_t)n_bytes)
    {
        perror("sendto");
        throw std::runtime_error("sendOutMsg: sendto failed");
    }
}

/**
 * @brief Receives a single InMsg from the UUT and stores it in the indexed slot.
 *
 * Waits to receive a 6-byte response and parses it into an `InMsg` structure.
 *
 * @param idx Index in the results/inMsgs array to store the parsed result.
 */
void HardwareTester::recvInMsg(int idx)
{
    sockaddr_in from;
    socklen_t from_len = sizeof(from);
    char recv_buf[IN_MSG_SIZE];

    int n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (sockaddr *)&from, &from_len);
    if (n != IN_MSG_SIZE) {
        perror("recvfrom");
        return;
    }

    InMsg& msg = inMsgs[idx];
    std::memcpy(&msg.test_id, &recv_buf[0], sizeof(uint32_t));
    msg.peripheral = recv_buf[4];
    msg.test_result = recv_buf[5];

    results[idx] = (msg.test_result == TEST_SUCCESS);
}

/**
 * @brief Retrieves the next available test ID from the logger.
 *
 * Calls `prep()` and then queries the next test ID.
 *
 * @param id Reference to store the result.
 * @return true if the operation succeeded.
 * @return false if an exception occurred.
 */
bool HardwareTester::getNextTestId(uint32_t &id)
{
    try
    {
        logger->prep();
    } catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return false;
    }

    try
    {
        id = logger->getNextId();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return false;
    }
    return true;
}

/**
 * @brief Formats a `timeval` into a human-readable timestamp string.
 *
 * @param buffer Output buffer to hold the formatted timestamp.
 * @param size Size of the buffer.
 * @param tv `timeval` structure to format.
 */
void HardwareTester::formatTimestamp(char *buffer, size_t size, const timeval &tv)
{
    struct tm *tm_info = localtime(&tv.tv_sec);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/**
 * @brief Computes the time elapsed in seconds between two timestamps.
 *
 * @param start Start time.
 * @param end End time.
 * @return double Elapsed time in seconds (with microsecond precision).
 */
double HardwareTester::elapsedSeconds(const timeval &start, const timeval &end)
{
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
}
