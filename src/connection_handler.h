#ifndef connection_handler_h_INCLUDED
#define connection_handler_h_INCLUDED

#include <map>

#include <asio.hpp>

#include "connection.h"

// A handler for connections
class ConnectionHandler {
public:
    // Constructs a connection handler for io_service, listening on port
    ConnectionHandler(asio::io_service& io_service, uint16_t port, asio::ip::address &gateway,
                      asio::ip::address &external);

private:
    // Callback for async_accept
    void accept_connection(const asio::error_code& ec);

    asio::ip::tcp::acceptor acceptor;
    asio::ip::tcp::socket socket;

    asio::ip::address gateway, external;

    std::map<uint64_t, std::weak_ptr<Connection>> connections;
};

#endif // connection_handler_h_INCLUDED
