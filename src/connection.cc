#include "connection.h"

#include <iostream>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htonll(x) ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32)
#define ntohll(x) ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32)
#else
#define htonll(x) (x)
#define ntohll(x) (x)
#endif

namespace std {
    using namespace placeholders;
} // namespace std

static const std::array<std::string, 5> type_strings = { "CONNECT", "OPEN", "ACCEPT", "ERROR", "CHAT" };

MessageType parse_message_type(std::string type)
{
    auto pos = std::find(type_strings.begin(), type_strings.end(), type);
    return static_cast<MessageType>(std::distance(type_strings.begin(), pos));
}

std::string get_message_string(MessageType type)
{
    return type_strings[type];
}

Connection::Connection(asio::ip::tcp::socket sock,
                       std::map<uint64_t, std::weak_ptr<Connection>>& conns)
    : socket(std::move(sock)),
      connections(conns)
{
}

Connection::~Connection()
{
    auto pos = connections.find(id);
    if (pos != connections.end()) {
        connections.erase(pos);
        std::cout << "Connection ID " << id << " closed" << std::endl;
    }
}

asio::ip::tcp::endpoint Connection::get_endpoint()
{
    return socket.remote_endpoint();
}

void Connection::start_connection()
{
    asio::async_read_until(socket, in_buf, '\n',
        [this, self = shared_from_this()](const asio::error_code& ec, size_t num) {
            if (!ec) {
                std::string line;
                std::istream is(&in_buf);
                std::getline(is, line);

                size_t idx = line.find(' ');
                if (line.substr(0, idx) != "OPEN")
                    return;

                size_t bytes;
                try {
                    bytes = std::stoul(line.substr(idx + 1));
                    if (bytes != 8)
                        return;
                } catch (std::invalid_argument& e) {
                    return;
                } catch (std::out_of_range& e) {
                    return;
                }

                auto read_func = [this, self](const asio::error_code& ec, size_t num) {
                    if (!ec) {
                        std::istream is(&in_buf);
                        is.read(reinterpret_cast<char *>(&id), sizeof(id));
                        id = ntohll(id);
                        if (connections.find(id) == connections.end())
                            connections[id] = self;
                        else
                            return;
                        auto end = get_endpoint();
                        std::cout << "Established connection from "
                            << end.address().to_string() << ":" << end.port() << ", ID " << id
                            << "." << std::endl;
                        asio::async_read_until(socket, in_buf, '\n',
                            std::bind(&Connection::read_callback, self, std::_1, std::_2));
                    }
                };

                if (bytes > in_buf.size())
                    asio::async_read(socket, in_buf, asio::transfer_exactly(bytes - in_buf.size()),
                                     read_func);
                else
                    read_func(asio::error_code(), 0);
            }
        });
}

void Connection::queue_write_message(MessageType type,
    const asio::const_buffer& buf)
{
    switch (type) {
        case CONNECT:
            if (asio::buffer_size(buf) != 6)
                throw std::invalid_argument("CONNECT expected an IPv4 address and port");
            break;
        case OPEN:
            if (asio::buffer_size(buf) != 8)
                throw std::invalid_argument("OPEN expected a uint64_t id");
            break;
        case ACCEPT:
            if (asio::buffer_size(buf) != 1)
                throw std::invalid_argument("ACCEPT expected a boolean byte");
            break;
        case ERROR:
        case CHAT:
            break;
        case INVALID:
            throw std::invalid_argument("Cannot send INVALID message");
    }

    std::string header = get_message_string(type) + " "
        + std::to_string(asio::buffer_size(buf)) + "\n";
    std::vector<asio::const_buffer> data;
    data.push_back(asio::buffer(header));
    data.push_back(buf);
    asio::async_write(socket, data, asio::transfer_all(),
                      std::bind(&Connection::write_callback, shared_from_this(),
                                std::_1, std::_2));
}

void Connection::read_callback(const asio::error_code& ec, size_t num)
{
    if (!ec) {
        std::string line;
        std::istream is(&in_buf);
        std::getline(is, line);

        size_t idx = line.find(' ');
        MessageType type = parse_message_type(line.substr(0, idx));

        size_t bytes;
        try {
            bytes = std::stoul(line.substr(idx + 1));
        } catch (std::invalid_argument& e) {
            return;
        } catch (std::out_of_range& e) {
            return;
        }

        std::function<void(std::istream&)> read_func;
        auto self = shared_from_this();

        switch (type) {
            case OPEN:
                if (bytes != 8)
                    return;
                read_func = std::bind(&Connection::read_open, self, std::_1);
                break;
            case ACCEPT:
                if (bytes != 1)
                    return;
                read_func = std::bind(&Connection::read_accept, self, std::_1);
                break;
            case CONNECT:
            case ERROR:
            case CHAT:
            case INVALID:
                return;
        }

        if (bytes > in_buf.size())
            asio::async_read(socket, in_buf, asio::transfer_exactly(bytes - in_buf.size()),
                             std::bind(&Connection::read_buffer, self,
                                       std::_1, std::_2, read_func));
        else
            read_buffer(asio::error_code(), 0, read_func);
    } else if (ec != asio::error::eof)
        std::cerr << "Asio error: ID " << id << " " << ec.value() << ", "
                  << ec.message() << std::endl;
}

void Connection::read_buffer(const asio::error_code& ec, size_t num,
    std::function<void(std::istream&)> func)
{
    if (!ec) {
        std::istream is(&in_buf);
        func(is);
        asio::async_read_until(socket, in_buf, '\n',
                               std::bind(&Connection::read_callback, shared_from_this(),
                                         std::_1, std::_2));
    } else if (ec != asio::error::eof) {
        std::cerr << "Asio error: ID " << id << ": " << ec.value() << ", "
                  << ec.message() << std::endl;
    }
}

void Connection::write_callback(const asio::error_code& ec, size_t num)
{
    if (ec && ec != asio::error::eof)
        std::cerr << "Asio error on connection ID " << id << ": " << ec.value() << ", "
                  << ec.message() << std::endl;
}

void Connection::read_open(std::istream& is)
{
    uint64_t other_id;
    is.read(reinterpret_cast<char *>(&other_id), sizeof(other_id));
    other_id = ntohll(other_id);
    auto pos = connections.find(other_id);
    if (pos == connections.end()) {
        queue_write_message(ERROR, asio::buffer("Peer ID " + std::to_string(other_id)
                                                + " not found"));
    } else if (pos->second.expired()) {
        // Shouldn't be possible, but just in case
        queue_write_message(ERROR, asio::buffer("Peer expired"));
    } else {
        auto other = pos->second.lock();
        other->requested_from = std::weak_ptr<Connection>(shared_from_this());
        uint64_t temp_id = htonll(id);
        other->queue_write_message(OPEN, asio::buffer(&temp_id, sizeof(id)));
    }
}

void Connection::read_accept(std::istream& is)
{
    bool accepted;
    is.read(reinterpret_cast<char *>(&accepted), sizeof(accepted));
    if (requested_from.expired()) {
        queue_write_message(ERROR,
                            asio::buffer("Peer disconnected or no connection requested"));
    } else if (accepted) {
        auto other = requested_from.lock();
        // TODO IPv6 support
        uint8_t buf_arr[6];
        asio::mutable_buffer buf(buf_arr, sizeof(buf_arr));
        asio::buffer_copy(buf, asio::buffer(other->get_endpoint().address().to_v4().to_bytes()));
        uint16_t port = htons(other->get_endpoint().port());
        asio::buffer_copy(buf + 4, asio::buffer(&port, sizeof(port)));
        queue_write_message(CONNECT, buf);

        uint8_t other_buf_arr[6];
        asio::mutable_buffer other_buf(other_buf_arr, sizeof(other_buf_arr));
        port = htons(get_endpoint().port());
        asio::buffer_copy(other_buf, asio::buffer(get_endpoint().address().to_v4().to_bytes()));
        uint16_t other_port = htons(get_endpoint().port());
        asio::buffer_copy(other_buf + 4, asio::buffer(&other_port, sizeof(other_port)));
        other->queue_write_message(CONNECT, other_buf);
    }
    requested_from = std::weak_ptr<Connection>();
}
