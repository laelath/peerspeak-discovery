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
    if (pos != connections.end())
        connections.erase(pos);
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

                size_t read = 0;
                if (bytes > in_buf.size())
                    read = bytes - in_buf.size();

                asio::async_read(socket, in_buf, asio::transfer_exactly(read),
                    [this, self](const asio::error_code& ec, size_t num) {
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
                    });
            }
        });
}

void Connection::queue_write_message(MessageType type,
    const asio::streambuf::const_buffers_type& buf)
{
    switch (type) {
        case CONNECT:
            if (asio::buffer_size(buf) != 4)
                throw std::invalid_argument("CONNECT expected an IPv4 address");
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
    std::vector<asio::streambuf::const_buffers_type> data;
    data.push_back(asio::buffer(header));
    data.push_back(buf);
    asio::async_write(socket, data, asio::transfer_all(),
        std::bind(&Connection::write_callback, shared_from_this(), std::_1, std::_2));
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

        size_t read = 0;
        if (bytes > in_buf.size())
            read = bytes - in_buf.size();

        auto read_func = [this, self = shared_from_this(), type, bytes]
            (const asio::error_code& ec, size_t num) {
            if (!ec) {
                std::istream is(&in_buf);
                if (type == OPEN) {
                    if (bytes != 8)
                        return;
                    uint64_t other_id;
                    is.read(reinterpret_cast<char *>(&other_id), sizeof(other_id));
                    other_id = ntohll(other_id);
                    auto pos = connections.find(other_id);
                    if (pos == connections.end()) {
                        queue_write_message(ERROR, asio::buffer("Peer ID "
                            + std::to_string(other_id) + " not found"));
                    } else if (pos->second.expired()) {
                        // Shouldn't be possible, but just in case
                        queue_write_message(ERROR, asio::buffer("Peer expired"));
                    } else {
                        auto other = pos->second.lock();
                        other->requested_from = std::weak_ptr<Connection>(shared_from_this());
                        uint64_t temp_id = htonll(id);
                        other->queue_write_message(OPEN,
                            static_cast<asio::streambuf::const_buffers_type>(
                                asio::buffer(&temp_id, sizeof(id))));
                    }
                } else if (type == ACCEPT) {
                    if (bytes != 1)
                        return;
                    bool accepted;
                    is.read(reinterpret_cast<char *>(&accepted), sizeof(accepted));
                    if (requested_from.expired()) {
                        queue_write_message(ERROR,
                            asio::buffer("Peer disconnected or no connection requested"));
                    } else if (accepted) {
                        auto other = requested_from.lock();
                        // TODO IPv6 support
                        uint16_t port = htons(other->get_endpoint().port());
                        std::array<asio::streambuf::const_buffers_type, 2> data = {
                            asio::buffer(other->get_endpoint().address().to_v4().to_bytes()),
                            asio::streambuf::const_buffers_type(&port, sizeof(port)) };
                        queue_write_message(CONNECT,
                            static_cast<asio::streambuf::const_buffers_type>(asio::buffer(data)));

                        port = htons(get_endpoint().port());
                        data = { asio::buffer(get_endpoint().address().to_v4().to_bytes()),
                            asio::streambuf::const_buffers_type(&port, sizeof(port)) };
                        other->queue_write_message(CONNECT,
                            static_cast<asio::streambuf::const_buffers_type>(asio::buffer(data)));
                    }
                    requested_from = std::weak_ptr<Connection>();
                } else
                    return;

                asio::async_read_until(socket, in_buf, '\n', std::bind(&Connection::read_callback,
                        self, std::_1, std::_2));
            } else if (ec == asio::error::eof)
                std::cout << "Connection ID " << id << " closed" << std::endl;
            else
                std::cerr << "Error on connection ID " << id << ": " << ec.value() << ", "
                    << ec.message() << "." << std::endl;
        };

        if (read == 0)
            read_func(asio::error_code(), 0);
        else
            asio::async_read(socket, in_buf, asio::transfer_exactly(read), read_func);
    } else if (ec == asio::error::eof)
        std::cout << "Connection ID " << id << " closed" << std::endl;
    else
        std::cerr << "Error on connection ID " << id << ": " << ec.value() << ", " << ec.message()
            << "." << std::endl;
}

void Connection::write_callback(const asio::error_code& ec, size_t num)
{
    if (ec && ec != asio::error::eof)
        std::cerr << "Error on connection ID " << id << ": " << ec.value() << ", " << ec.message()
            << "." << std::endl;
}
