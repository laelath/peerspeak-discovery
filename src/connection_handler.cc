#include "connection_handler.h"

namespace std {
    using namespace placeholders;
} // namespace std

ConnectionHandler::ConnectionHandler(asio::io_service& io_service, uint16_t port)
    : acceptor(io_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      socket(io_service)
{
    acceptor.async_accept(socket, std::bind(&ConnectionHandler::accept_connection, this, std::_1));
}

void ConnectionHandler::accept_connection(const asio::error_code& ec)
{
    if (!ec) {
        auto conn = std::make_shared<Connection>(std::move(socket), connections);
        conn->start_connection();
    }

    acceptor.async_accept(socket, std::bind(&ConnectionHandler::accept_connection, this, std::_1));
}
