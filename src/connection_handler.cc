#include "connection_handler.h"

using namespace std::placeholders;

ConnectionHandler::ConnectionHandler(asio::io_service& io_service, uint16_t port,
                                     asio::ip::address &gateway, asio::ip::address &external)
    : acceptor(io_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      socket(io_service),
      gateway(gateway),
      external(external)
{
    acceptor.async_accept(socket, std::bind(&ConnectionHandler::accept_connection, this, _1));
}

void ConnectionHandler::accept_connection(const asio::error_code& ec)
{
    if (!ec) {
        auto conn = std::make_shared<Connection>(socket.get_io_service(), std::move(socket),
                                                 gateway, external, connections);
        conn->start_connection();
    }

    acceptor.async_accept(socket, std::bind(&ConnectionHandler::accept_connection, this, _1));
}
