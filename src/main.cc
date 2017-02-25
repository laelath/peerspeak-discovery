#include <iostream>
#include <thread>
#include <vector>

#include <asio.hpp>

#include <glibmm.h>

#define PORT 2784

enum MessageType {
    CONNECT,
    OPEN,
    ACCEPT,
    CHAT,
};

class connection : public std::enable_shared_from_this<connection> {
public:
    connection(asio::ip::tcp::socket socket, std::vector<std::shared_ptr<connection>>& connections);
    ~connection();

    void queue_read_message();
    void queue_write_message(MessageType type, const std::vector<uint8_t>& buf);

    uint64_t getID() { return id; }

private:
    void read_callback(const asio::error_code& ec, size_t num);
    void write_callback(const asio::error_code& ec, size_t num);

    asio::ip::tcp::socket socket;
    asio::streambuf in_buf;
    asio::streambuf out_buf;

    uint64_t id;

    std::vector<std::shared_ptr<connection>>& connections;
};

connection::connection(asio::ip::tcp::socket socket, std::vector<std::shared_ptr<connection>>& connections)
    : socket(std::move(socket)),
      connections(connections)
{
    asio::async_read_until(socket, in_buf, '\n',
        [self = shared_from_this()](const asio::error_code& ec, size_t num) {
            if (!ec) {
                // TODO Read buffer and determine whether connection is valid
            }
        });
}

connection::~connection()
{
    socket.close();
}

class conn_handler {
public:
    conn_handler(asio::io_service& io_service, int port);

private:
    void accept_connection(const asio::error_code& ec);

    asio::ip::tcp::acceptor acceptor;
    asio::ip::tcp::socket socket;

    std::vector<std::shared_ptr<connection>> connections;
};

conn_handler::conn_handler(asio::io_service& io_service, int port)
    : acceptor(io_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      socket(io_service)
{
    acceptor.async_accept(socket, sigc::mem_fun(this, &conn_handler::accept_connection));
}

void conn_handler::accept_connection(const asio::error_code& ec)
{
    if (!ec) {
        std::make_shared<connection>(std::move(socket), connections);
    }

    acceptor.async_accept(socket, sigc::mem_fun(this, &conn_handler::accept_connection));
}

void parse_command_line(int argc, char *argv[], int& port)
{
    Glib::OptionEntry entry;
    entry.set_long_name("port");
    entry.set_short_name('p');
    entry.set_description("Set the port for the server to listen on, defaults to "
                          + std::to_string(PORT) + ".");

    Glib::OptionGroup group("", "");
    group.add_entry(entry, port);

    Glib::OptionContext context;
    context.set_main_group(group);
    context.parse(argc, argv);
}

int main(int argc, char *argv[])
{
    int port = PORT;
    parse_command_line(argc, argv, port);

    asio::io_service io_service;

    conn_handler handler(io_service, port);

    io_service.run();
}
