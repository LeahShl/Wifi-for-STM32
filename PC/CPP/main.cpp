// main.cpp

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include "HardwareTester.hpp"
#include "TestLogger.hpp"

#define ARGS_ERROR 1                   // Error parsing command line arguments
#define NETWORK_ERROR 2                // UDP communication error
#define DB_ERROR 3                     // SQLite3 database error

void print_usage(const std::string& progName);

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return ARGS_ERROR;
    }

    std::string first_arg = argv[1];
    TestLogger logger;

    if (first_arg == "get")
    {
        if (argc < 3)
        {
            std::cerr << "Error: 'get' requires at least one test ID\n";
            return ARGS_ERROR;
        }

        try
        {
            logger.prep();
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return DB_ERROR;
        }

        for (int i = 2; i < argc; ++i)
        {
            uint32_t id = std::stoul(argv[i]);
            std::cout << logger.strById(id) << "\n";
        }
        return 0;

    }
    else if (first_arg == "export")
    {
        if (argc > 2)
        {
            std::cerr << "Error: 'export' takes no arguments\n";
            return ARGS_ERROR;
        }

        try
        {
            logger.prep();
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return DB_ERROR;
        }

        std::cout << logger.exportAll();
        return EXIT_SUCCESS;

    }
    else
    {
        // Parse options
        bool want_u = false, want_s = false, want_i = false;
        std::string msg_u, msg_s, msg_i;
        bool got_u = false, got_s = false, got_i = false;
        bool used_all = false, used_n = false;
        uint n_iter = N_ITERATIONS;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "-h" || arg == "--help")
            {
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            }
            else if (arg == "--all")
            {
                if (used_all)
                {
                    std::cerr << "Error: --all used multiple times\n";
                    return ARGS_ERROR;
                }
                want_u = want_s = want_i = true;
                used_all = true;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    msg_u = msg_s = msg_i = argv[++i];
                    got_u = got_s = got_i = true;
                }
            }
            else if (arg == "-n")
            {
                if (used_n || i + 1 >= argc)
                {
                    std::cerr << "Error: '-n' must be followed by a number (0-255)\n";
                    return ARGS_ERROR;
                }
                n_iter = std::stoi(argv[++i]);
                if (n_iter > 255)
                {
                    std::cerr << "Error: '-n' must be in range 0-255\n";
                    return ARGS_ERROR;
                }
                used_n = true;
            }
            else if (arg[0] == '-' && arg[1] != '\0')
            {
                for (size_t j = 1; j < arg.size(); ++j)
                {
                    switch (arg[j])
                    {
                        case 'u':
                            if (want_u) { std::cerr << "Error: -u repeated\n"; return 1; }
                            want_u = true;
                            break;
                        case 's':
                            if (want_s) { std::cerr << "Error: -s repeated\n"; return 1; }
                            want_s = true;
                            break;
                        case 'i':
                            if (want_i) { std::cerr << "Error: -i repeated\n"; return 1; }
                            want_i = true;
                            break;
                        default:
                            std::cerr << "Error: Unknown option -" << arg[j] << "\n";
                            return ARGS_ERROR;
                    }
                }

                // Look ahead for shared message
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    std::string msg = argv[++i];
                    if (want_u) { msg_u = msg; got_u = true; }
                    if (want_s) { msg_s = msg; got_s = true; }
                    if (want_i) { msg_i = msg; got_i = true; }
                }
            }
            else
            {
                std::cerr << "Unexpected token: " << arg << "\n";
                return ARGS_ERROR;
            }
        }

        if (!(want_u || want_s || want_i))
        {
            std::cerr << "Error: must specify at least one test (-u, -s, -i or --all)\n";
            return ARGS_ERROR;
        }

        // Fill defaults
        if (want_u && !got_u) msg_u = "Hello UART";
        if (want_s && !got_s) msg_s = "Hello SPI";
        if (want_i && !got_i) msg_i = "Hello I2C";

        HardwareTester tester;
        if (!tester.connect())
        {
            std::cerr << "Network connection failed\n";
            return NETWORK_ERROR;
        }

        uint8_t flags = 0;
        std::string shared;

        if (want_u) flags |= TEST_UART;
        if (want_s) flags |= TEST_SPI;
        if (want_i) flags |= TEST_I2C;

        // Pick one of the messages arbitrarily for the payload (they are identical in --all case)
        if (want_u) shared = msg_u;
        else if (want_s) shared = msg_s;
        else if (want_i) shared = msg_i;

        tester.runTests(flags, n_iter, shared);
        std::cout << tester.strLast() << "\n";

        return EXIT_SUCCESS;
    }
}

void print_usage(const std::string& progName)
{
    std::cout <<
        "Usage: " << progName << " [OPTIONS]\n"
        "       " << progName << " [COMMAND]\n"
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
        "  export                Print all available tests data in a csv format\n";
}