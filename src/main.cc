#include <iostream>
#include <vector>

#include <asio.hpp>
#include <optionparser.h>

#include "connection_handler.h"

static const uint16_t default_port = 2738;

// Parses the command line arguments
void parse_command_line(int argc, char *argv[], uint16_t& port)
{
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
                std::cerr << "Option '" << option.name << "'requires a numeric argument."
                    << std::endl;
            return option::ARG_ILLEGAL;
        }
    };

    enum OptionIndex { UNKNOWN, HELP, PORT };

    const std::string usage_str[] = {
        "USAGE: " + std::string(argv[0]) + " [options]\n\nOptions:",
        "  --help, -h \tPrint usage and exit.",
        "  --port, -p <num> \tSet the port to listen on, defaults to " +
            std::to_string(default_port) + ".",
    };

    const option::Descriptor usage[] = {
        { UNKNOWN, 0, "",  "",     Arg::None,    usage_str[UNKNOWN].c_str() },
        { HELP,    0, "h", "help", Arg::None,    usage_str[HELP].c_str()    },
        { PORT,    0, "p", "port", Arg::Numeric, usage_str[PORT].c_str()    },
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
    } else if (options[PORT]) {
        int num = std::stol(options[PORT].arg);
        if (num <= 1023 || num > UINT16_MAX) {
            std::cerr << "Port number must be between 1024 and " << UINT16_MAX << "." << std::endl;
            std::exit(EXIT_FAILURE);
        }
        port = num;
    }
}

int main(int argc, char *argv[])
{
    uint16_t port = default_port;
    parse_command_line(argc, argv, port);

    asio::io_service io_service;
    ConnectionHandler handler(io_service, port);

    io_service.run();
    return EXIT_SUCCESS;
}
