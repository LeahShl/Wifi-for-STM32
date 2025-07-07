/*
 * @file main.c
 *
 * @brief Tester program for Linux, accompanying an STM32F756ZG testing project.
 *
 * USAGE:
 *  1) At least one flag (u, s, i, or --all) must be present.
 *  2) No flag may appear more than once.
 *  3) Flags can be stacked (e.g. -usi) 
 *  4) If a stack includes (u, s, or i) and is immediately followed by a non-flag token,
 *     that token is taken as the single message for all of {u,s,i} in the stack.
 *  6) Separate flags like `-u "msg"` are allowed; same rules for message.
 *  7) Set number of test iterations with -n <int>, for example '-n 20'
 *  8) Use --all to run all tests with a single message and receive results concurrently.
 *     Stacked flags like '-usi' also run tests in parallel with a shared message.
 *
 * DATA RETRIEVING
 *  +) Use 'get' and 'export' to retrieve data
 *  +) 'get' prints to stdout test data by test ID
 *  +) 'export' prints to stdout all test data in a csv format (redirect to a file to save)
 */

#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "tests_db.h"

/*************************
 * MACROS                *
 *************************/

#define UUT_ADDR "192.168.1.177"    // IP address of Unit Under Test (UUT)
#define PORT 54321                 // Port for UDP communication
#define BUFSIZE 263                // Max possible size of OutMsg
#define IN_MSG_SIZE 6              // Incoming msg is always 6 bytes

#define TEST_UART 2                // UART test code
#define TEST_SPI 4                 // SPI test code
#define TEST_I2C 8                 // I2C test code

#define TEST_SUCCESS 0x01          // Test success code
#define TEST_FAILED 0xff           // Test failed code

#define N_ITERATIONS 1             // Default number of test iterations
#define N_TESTS 3                  // Total number of test types

#define ARGS_ERROR 1               // Error parsing command line arguments
#define UDP_ERROR 2                // UDP communication error
#define SQLITE_ERROR 3             // SQLite3 database error

/*************************
 * TYPEDEFS              *
 *************************/

/**
 * @brief Holds data for outgoing communication
 * 
 * @struct OutMsg
 */
typedef struct OutMsg
{
	uint32_t test_id;              /** Unique test ID */
	uint8_t peripheral;            /** Peripheral code */
	uint8_t n_iter;                /** Number of iterations */
	uint8_t p_len;                 /** Payload length */
	char payload[256];             /** Payload buffer */
}OutMsg;

/**
 * @brief Holds data for incoming communication
 * 
 * @struct InMsg
 */
typedef struct InMsg
{
	uint32_t test_id;              /** Unique test ID */
    uint8_t peripheral;            /** Peripheral code */
	uint8_t test_result;           /** Test result (success/fail) */
}InMsg;

/**
 * @brief Arguments for receiver threads
 * 
 * @struct RecvThreadArgs
 */
typedef struct RecvThreadArgs
{
    int idx;                       /** Thread index */
    struct InMsg *recv;            /** Received message data */
}RecvThreadArgs;

/*************************
 * GLOBALS               *
 *************************/

static const char *DEFAULT_U_MSG = "Hello UART"; /** Default message (bit pattern) for UART test */
static const char *DEFAULT_S_MSG = "Hello SPI";  /** Default message (bit pattern) for SPI test */
static const char *DEFAULT_I_MSG = "Hello I2C";  /** Default message (bit pattern) for I2C test */

/**
 * Globals for UDP communication
 */
static int sock;
static struct sockaddr_in sock_addr;
static struct hostent *host;
static char buf[BUFSIZE];

static struct OutMsg out_msg;

/**
 * Globals for multithreading
 */
static pthread_t threads[N_TESTS];
static int n_threads = 0;
static sem_t tests_done_sem;
static struct InMsg in_msgs[N_TESTS];
static int results[N_TESTS];

/*************************
 * FUNCTION DECLERATIONS *
 *************************/

/**
 * @brief Print usage info
 * 
 * @param progname Program's name
 */
static void print_usage(const char *progname);

/**
 * @brief Receiver thread function
 * 
 * @param arg Pointer to struct RecvThreadArgs
 * @return void* Always NULL
 */
static void *recv_thread(void *arg);

/**
 * @brief Perform peripherals test concurrently
 * 
 * @param peripherals Peripherals code bitfield
 * @param n_iter Number of test iterations
 * @param shared_msg Bit pattern for the test
 */
static void run_parallel_tests(uint8_t peripherals, uint8_t n_iter, const char *shared_msg);

/**
 * @brief Format timestamp into a string
 * 
 * @param tv Timestamp object
 * @param buffer Destination buffer
 * @param size Size of buffer
 */
static void format_timestamp(struct timeval *tv, char *buffer, size_t size);

/**
 * @brief Get the elapsed seconds between start and end
 * 
 * @param start Start timestamp
 * @param end End timestamp
 * @return double Elapsed seconds
 */
static double get_elapsed_seconds(struct timeval start, struct timeval end);

/**
 * @brief Initiates udp network
 * 
 */
static void udp_init_network();

/**
 * @brief Send OutMsg data via udp socket
 * 
 * @attention OutMsg must be loaded before calling this function
 */
static void udp_send_data();

/**
 * @brief receive udp data and load it to InMsg
 * 
 */
static void udp_receive_data(struct InMsg *in_msg);

/*************************
 * MAIN                  *
 *************************/

int main(int argc, char *argv[])
{
    bool want_u = false, want_s = false, want_i = false;
    bool seen_u = false, seen_s = false, seen_i = false;
    bool used_all = false, used_n = false;
    const char *msg_u = NULL, *msg_s = NULL, *msg_i = NULL;
    bool have_msg_u = false, have_msg_s = false, have_msg_i = false;

    uint8_t n = N_ITERATIONS;

    if (argc < 2)
    {
        print_usage(argv[0]);
        exit(ARGS_ERROR);
    }

    // Handle special commands
    if (strcmp(argv[1], "get") == 0)
    {
        if (argc < 3)
        {
            perror("Error: 'get' command requires at least one ID.\n");
            exit(ARGS_ERROR);
        }

        int db_success = init_db();
        if (!db_success)
        {
	        perror("databse init failed");
		    exit(SQLITE_ERROR);
	    }

        for (int i = 2; i < argc; ++i)
        {
            char *endptr = NULL;
            long tid = strtol(argv[i], &endptr, 10);

            if (*endptr != '\0' || tid < 0)
            {
                fprintf(stderr, "Error: Invalid test ID '%s'. Must be non-negative integer.\n", argv[i]);
                exit(ARGS_ERROR);
            }

            print_log_by_id((uint32_t)tid);
        }
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(argv[1], "export") == 0)
    {
        if (argc > 2)
        {
            perror("Error: 'export' does not take any arguments.\n");
            exit(ARGS_ERROR);
        }

        int db_success = init_db();
        if (!db_success)
        {
	        perror("databse init failed");
		    exit(SQLITE_ERROR);
	    }

        print_all_logs();

        exit(EXIT_SUCCESS);
    }

    // Handle options
    int idx = 1;
    while (idx < argc)
    {
        char *arg = argv[idx];

        // Print help menu
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }

        // Handle --all flag
        if (strcmp(arg, "--all") == 0)
        {
            if (used_all)
            {
                perror("Error: '--all' cannot be repeated");
                exit(ARGS_ERROR);
            }
            used_all = true;
            want_u = want_s = want_i = true;
            seen_u = seen_s = seen_i = true;
            
            // Check for message
            if ( (idx + 1) < argc && argv[idx+1][0] != '-' )
            {
                const char *shared = argv[idx+1];
                msg_u = msg_s = msg_i = shared;
                have_msg_u = have_msg_s = have_msg_i = true;
                idx += 1;
            }
            idx += 1;
            continue;
        }

        // Handle -n flag
        if (strcmp(arg, "-n") == 0)
        {
            if (used_n) {
                perror("Error: '-n' cannot be repeated");
                exit(ARGS_ERROR);
            }
            if ((idx + 1) >= argc)
            {
                perror("Error: '-n' requires [0-255] value.\n");
                exit(ARGS_ERROR);
            }

            char *endptr = NULL;
            long val = strtol(argv[idx + 1], &endptr, 10);
            if (*endptr != '\0' || val < 0 || val > 255)
            {
                perror("Error: '-n' requires [0-255] value.\n");
                exit(ARGS_ERROR);
            }
            n = (uint8_t)val;
            used_n = true;
            idx += 2;
            continue;
        }

        // Handle all other flags
        if (arg[0] == '-' && arg[1] != '\0' && arg[1] != '-')
        {
            size_t len = strlen(arg);
            bool stack_has_u = false, stack_has_s = false, stack_has_i = false;
            for (size_t k = 1; k < len; ++k) {
                char c = arg[k];
                switch (c)
                {
                    case 'u':
                        if (seen_u) { perror("Error: '-u' repeated"); exit(ARGS_ERROR); }
                        seen_u = true; want_u = true; stack_has_u = true;
                        break;
                    case 's':
                        if (seen_s) { perror("Error: '-s' repeated"); exit(ARGS_ERROR); }
                        seen_s = true; want_s = true; stack_has_s = true;
                        break;
                    case 'i':
                        if (seen_i) { perror("Error: '-i' repeated"); exit(ARGS_ERROR); }
                        seen_i = true; want_i = true; stack_has_i = true;
                        break;
                    default:
                        fprintf(stderr, "Error: Unknown option '-%c'.\n", c);
                        exit(ARGS_ERROR);
                }
            }

            // Check for message string
            bool next_is_msg = false;
            if ( (idx + 1) < argc && argv[idx+1][0] != '-' )
            {
                next_is_msg = true;
            }

            if (next_is_msg)
            {
                const char *shared = argv[idx+1];
                if (stack_has_u) { msg_u = shared; have_msg_u = true; }
                if (stack_has_s) { msg_s = shared; have_msg_s = true; }
                if (stack_has_i) { msg_i = shared; have_msg_i = true; }
                idx += 1;
            }

            idx += 1;
            continue;
        }

        // If reached here, there's an illegal argument
        fprintf(stderr, "Error: Unexpected token '%s'.\n", arg);
        exit(ARGS_ERROR);
    }

    if (!(want_u || want_s || want_i))
    {
        perror("Error: At least one of -u, -s, -i, or --all must be provided");
        print_usage(argv[0]);
        exit(ARGS_ERROR);
    }


    // If reached here, then all the flags are ok

    if (want_u && !have_msg_u)
    {
        msg_u = DEFAULT_U_MSG;
    }
    if (want_s && !have_msg_s)
    {
        msg_s = DEFAULT_S_MSG;
    }
    if (want_i && !have_msg_i)
    {
        msg_i = DEFAULT_I_MSG;
    }
    
    udp_init_network();
    int db_success = init_db();
    if (!db_success)
    {
		perror("databse init failed");
		exit(SQLITE_ERROR);
	}

    if (want_u || want_s || want_i)
    {
        uint8_t peripheral_flags = 0;
        const char *shared_msg = NULL;

        if (want_u) peripheral_flags |= TEST_UART;
        if (want_s) peripheral_flags |= TEST_SPI;
        if (want_i) peripheral_flags |= TEST_I2C;

        if ((want_u && have_msg_u && msg_u) ||
            (want_s && have_msg_s && msg_s) ||
            (want_i && have_msg_i && msg_i))
        {
            if (want_u) shared_msg = msg_u;
            else if (want_s) shared_msg = msg_s;
            else if (want_i) shared_msg = msg_i;
        }
        else
        {
            shared_msg = DEFAULT_U_MSG;
        }

        run_parallel_tests(peripheral_flags, n, shared_msg);
    }

    return EXIT_SUCCESS;
}

/****************************
 * FUNCTION IMPLEMENTATION  *
 ****************************/

static void print_usage (const char *progname)
{
    fprintf(stdout,
        "Usage: %s [OPTIONS]\n"
        "       %s [COMMAND]\n"
        "OPTIONS:\n"
        "  -n <int>       Optional: set number (0-255) of test iterations\n"
        "  -u [\"msg\"]   Run UART test (with optional message, default if none)\n"
        "  -s [\"msg\"]   Run SPI test (with optional message, default if none)\n"
        "  -i [\"msg\"]   Run I2C test (with optional message, default if none)\n"
        "  --all [\"msg\"]  Run all five tests (u,s,i use msg or their defaults)\n"
        "  -h, --help    Show this help and exit\n\n"
        "Flags u, s, i may be stacked (e.g. -usi). If stacked, you may supply exactly\n"
        "one message immediately after the entire stack (applies to all of u,s,i). Example:\n"
        "    %s -si \"shared message\" -u\n"
        "in a stack, you cannot follow that stack with a message.\n\n"
        "At least one of u, s, i (or --all) must be provided. No letter may appear twice.\n"
        "\n"
        "COMMANDS:\n"
        "  get <id1> <id2> ...   Print test data by test ID\n"
        "  export                Print all available tests data in a csv format\n",
        progname, progname, progname
    );
}

static void *recv_thread(void *arg)
{
    struct RecvThreadArgs
    *args = (struct RecvThreadArgs*)arg;

    udp_receive_data(args->recv);
    results[args->idx] = (args->recv->test_result == TEST_SUCCESS);
    sem_post(&tests_done_sem);

    free(arg);
    return NULL;
}

static void run_parallel_tests(uint8_t peripherals, uint8_t n_iter, const char *shared_msg)
{
    int load_success = get_next_id(&out_msg.test_id);
    if (!load_success)
    {
        perror("loading id from database failed");
        exit(SQLITE_ERROR);
    }

    out_msg.n_iter = n_iter;
    out_msg.p_len = strlen(shared_msg);
    strncpy(out_msg.payload, shared_msg, strlen(shared_msg));

    udp_init_network();
    if (!init_db())
    {
        perror("databse init failed");
        exit(SQLITE_ERROR);
    }

    n_threads = 0;

    if (peripherals & TEST_UART) n_threads++;
    if (peripherals & TEST_I2C) n_threads++;
    if (peripherals & TEST_SPI) n_threads++;

    out_msg.peripheral = peripherals;

    struct timeval start_time, end_time;

    gettimeofday(&start_time, NULL);
    udp_send_data();

    sem_init(&tests_done_sem, 0, 0);
    for (int i = 0; i < n_threads; ++i)
    {
        struct RecvThreadArgs *args = malloc(sizeof(struct RecvThreadArgs));
        args->idx = i;
        args->recv = &in_msgs[i];
        pthread_create(&threads[i], NULL, recv_thread, args);
    }

    for (int i = 0; i < n_threads; ++i)
        sem_wait(&tests_done_sem);

    for (int i = 0; i < n_threads; ++i)
        pthread_join(threads[i], NULL);

    gettimeofday(&end_time, NULL);

    char timestamp[64];
    format_timestamp(&start_time, timestamp, 64);
    double duration = get_elapsed_seconds(start_time, end_time);

    int all_success = 1;
    for (int i = 0; i < n_threads; ++i)
    {
        if (!results[i])
        {
            all_success = 0;
        } 
    }

    int log_success = log_test(out_msg.test_id, timestamp, duration, all_success);
    if(!log_success)
    {
        perror("error logging to database");
        exit(SQLITE_ERROR);
    }

    print_log_by_id(out_msg.test_id);

    sem_destroy(&tests_done_sem);
}

static void format_timestamp (struct timeval *tv, char *buffer, size_t size)
{
	struct tm *tm_info = localtime(&tv->tv_sec);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static double get_elapsed_seconds (struct timeval start, struct timeval end)
{
	return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_usec - start.tv_usec) / 1e6;
}

static void udp_init_network ()
{
    host = (struct hostent *) gethostbyname((char *)UUT_ADDR);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket");
        exit(UDP_ERROR);
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(PORT);
    sock_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(sock_addr.sin_zero),8);
}

static void udp_send_data ()
{	
	// load buffer
	size_t n_bytes = 0;
	
	memcpy(&buf[0], &out_msg.test_id, sizeof(int32_t));
	n_bytes += sizeof(int32_t);
	
	buf[n_bytes++] = out_msg.peripheral;
	buf[n_bytes++] = out_msg.n_iter;
	buf[n_bytes++] = out_msg.p_len;
	
	// has payload
	if(out_msg.p_len > 0)
	{
		memcpy(&buf[n_bytes], out_msg.payload, out_msg.p_len);
		n_bytes += out_msg.p_len;
	}
	
	// send
	int sent_bytes = sendto(sock, buf, n_bytes, 0, (struct sockaddr *)&sock_addr,
	                         sizeof(struct sockaddr));
	if (sent_bytes < 0)
	{
		perror("udp_send_data: socket error");
		exit(UDP_ERROR);
	}
	if ((size_t)sent_bytes != n_bytes)
	{
		perror("udp_send_data: incomplete transaction");
		exit(UDP_ERROR);
	}
}

static void udp_receive_data(struct InMsg *in_msg)
{
    struct sockaddr_in from_addr;
	socklen_t from_len = sizeof(from_addr);
	char recv_buf[sizeof(in_msg)];
	
	int bytes_read = recvfrom(sock, recv_buf, sizeof(in_msg), 0,
	                          (struct sockaddr *)&from_addr, &from_len);
	if (bytes_read < 0)
	{
		perror("udp_receive_data: socket error");
		exit(UDP_ERROR);
	}
	if (bytes_read != IN_MSG_SIZE)
	{
		perror("udp_receive_data: incomplete transaction");
        printf("Expected %d bytes, got %d\n", IN_MSG_SIZE, bytes_read);
		exit(UDP_ERROR);
	}

	// load in_msg
	memcpy(&in_msg->test_id, recv_buf, sizeof(in_msg->test_id));
    in_msg->peripheral = recv_buf[sizeof(in_msg->test_id)];
	in_msg->test_result = recv_buf[sizeof(in_msg->test_id) + sizeof(in_msg->peripheral)];
	
    printf("Test #%d | peripheral %d | result %d\n", in_msg->test_id, in_msg->peripheral, in_msg->test_result);
}