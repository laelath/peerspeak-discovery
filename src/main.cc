#include <iostream>
#include <vector>

#include <asio.hpp>
#include <optionparser.h>

#include "connection_handler.h"

static const uint16_t default_port = 2738;

struct Arg : public option::Arg {
    static option::ArgStatus Numeric(const option::Option& option, bool msg)
    {
        try {
            size_t num = 0;
            if (option.arg != 0)
                std::stol(option.arg, &num);
            if (num != 0 && num == strlen(option.arg))
                return option::ARG_OK;
        } catch (std::invalid_argument& e) {
        } catch (std::out_of_range& e) {
        }
        if (msg)
            std::cerr << "Option '" << option.name << "' requires a numeric argument" << std::endl;
        return option::ARG_ILLEGAL;
    }
    static option::ArgStatus IP(const option::Option &option, bool msg)
    {
        if (option.arg != 0) {
            asio::error_code ec;
            asio::ip::address::from_string(option.arg, ec);
            if (not ec)
                return option::ARG_OK;
        }
        if (msg)
            std::cerr << "Option '" << option.name << "' requires a valid IP address" << std::endl;
        return option::ARG_ILLEGAL;
    }
};

// Parses the command line arguments
void parse_command_line(int argc, char *argv[], uint16_t &port, asio::ip::address &gateway,
                        asio::ip::address &external)
{
    enum OptionIndex { UNKNOWN, HELP, PORT, GATEWAY, EXTERNAL };

    const std::string usage_str[] = {
        "USAGE: " + std::string(argv[0]) + " [options]\n\nOptions:",
        "  --help, \t-h \tPrint usage and exit",
        "  --port, \t-p <num> \tSet the port to listen on, defaults to " + std::to_string(default_port),
        "  --gateway, \t-g <gateway> \tSets the default gateway ip for running behind a NAT",
        "  --external, \t-e <external> \tSets the external ip for running behind a NAT",
    };

    const option::Descriptor usage[] = {
        { UNKNOWN,  0, "",  "",         Arg::None,    usage_str[UNKNOWN].c_str()  },
        { HELP,     0, "h", "help",     Arg::None,    usage_str[HELP].c_str()     },
        { PORT,     0, "p", "port",     Arg::Numeric, usage_str[PORT].c_str()     },
        { GATEWAY,  0, "g", "gateway",  Arg::IP,      usage_str[GATEWAY].c_str()  },
        { EXTERNAL, 0, "e", "external", Arg::IP,      usage_str[EXTERNAL].c_str() },
        { 0, 0, 0, 0, 0, 0 }
    };

    argc -= (argc > 0);
    argv += (argc > 0);
    option::Stats stats(usage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.options_max];
    option::Parser parse(usage, argc, argv, options, buffer);

    if (parse.error())
        std::exit(EXIT_FAILURE);

    if (options[HELP]) {
        option::printUsage(std::cout, usage);
        std::exit(EXIT_SUCCESS);
    }

    if (options[PORT]) {
        int num = std::stol(options[PORT].arg);
        if (num <= 1023 || num > UINT16_MAX) {
            std::cerr << "Port number must be between 1024 and " << UINT16_MAX << "." << std::endl;
            std::exit(EXIT_FAILURE);
        }
        port = num;
    }

    if (options[GATEWAY])
        gateway = asio::ip::address::from_string(options[GATEWAY].arg);
    if (options[EXTERNAL])
        external = asio::ip::address::from_string(options[EXTERNAL].arg);
}

int main(int argc, char *argv[])
{
    uint16_t port = default_port;
    asio::ip::address gateway, external;
    parse_command_line(argc, argv, port, gateway, external);

    asio::io_service io_service;
    ConnectionHandler handler(io_service, port, gateway, external);

    io_service.run();
    return EXIT_SUCCESS;
}
