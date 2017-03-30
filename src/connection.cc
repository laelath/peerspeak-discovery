#include "connection.h"

#include <iostream>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htonll(x) ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32)
#define ntohll(x) ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32)
#else
#define htonll(x) (x)
#define ntohll(x) (x)
#endif

using namespace std::placeholders;

const constexpr size_t message_header_length = sizeof(uint8_t) + sizeof(uint16_t);

//static const std::array<std::string, 5> type_strings = {
    //"CONNECT", "OPEN", "ACCEPT", "ERROR", "CHAT" };

/*MessageType parse_message_type(std::string type)
{
    auto pos = std::find(type_strings.begin(), type_strings.end(), type);
    return static_cast<MessageType>(std::distance(type_strings.begin(), pos));
}

std::string get_message_string(MessageType type)
{
    return type_strings[type];
}*/

Connection::Connection(asio::io_service& io_service, asio::ip::tcp::socket sock,
                       asio::ip::address &gateway, asio::ip::address &external,
                       std::map<uint64_t, std::weak_ptr<Connection>>& conns)
    : socket(std::move(sock)),
      timer(io_service, std::chrono::seconds(10)),
      gateway(gateway, socket.remote_endpoint().port()),
      external(external, socket.remote_endpoint().port()),
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
    if (socket.remote_endpoint() == gateway)
        return external;
    return socket.remote_endpoint();
}

void Connection::start_connection()
{
    auto self = shared_from_this();
    timer.async_wait(
        [this, self](const asio::error_code& ec) {
            if (!ec)
                socket.close();
        });
    asio::async_read(socket, in_buf, asio::transfer_exactly(message_header_length),
                     std::bind(&Connection::read_start_message, self, _1, _2));
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
    case ADD:
        if (asio::buffer_size(buf) != 8)
            throw std::invalid_argument("ADD expected a uint64_t id");
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

    //std::string header = get_message_string(type) + " "
        //+ std::to_string(asio::buffer_size(buf)) + "\n";
    //std::vector<asio::const_buffer> data;
    //data.push_back(asio::buffer(header));
    //data.push_back(buf);

    uint8_t net_type = type;
    uint16_t net_len = htons(asio::buffer_size(buf));

    std::array<asio::const_buffer, 3> data = {
        asio::buffer(&net_type, sizeof net_type),
        asio::buffer(&net_len, sizeof net_len),
        buf
    };
    asio::async_write(socket, data, asio::transfer_all(),
                      std::bind(&Connection::write_callback, shared_from_this(), _1, _2));
}

void Connection::read_start_message(const asio::error_code& ec, size_t num)
{
    if (!ec) {
        std::istream is(&in_buf);

        uint8_t net_type;
        uint16_t bytes;

        is.read(reinterpret_cast<char *>(&net_type), sizeof net_type);
        is.read(reinterpret_cast<char *>(&bytes), sizeof bytes);

        MessageType type = static_cast<MessageType>(net_type);
        bytes = ntohs(bytes);

        if (type != OPEN)
            return;

        asio::async_read(socket, in_buf, asio::transfer_exactly(bytes),
                         std::bind(&Connection::read_start_buffer, shared_from_this(), _1, _2));
    }
}

void Connection::read_start_buffer(const asio::error_code& ec, size_t num)
{
    if (!ec) {
        timer.cancel();
        auto self = shared_from_this();
        std::istream is(&in_buf);
        is.read(reinterpret_cast<char *>(&id), sizeof(id));
        id = ntohll(id);
        if (connections.find(id) == connections.end())
            connections[id] = self;
        else {
            queue_write_message(ERROR, asio::buffer("ID " + std::to_string(id)
                                                    + " already exists in server"));
            id = 0;
            return;
        }

        auto end = get_endpoint();
        std::cout << "Established connection from " << end.address().to_string() << ":"
                  << end.port() << ", ID " << id << std::endl;
        asio::async_read(socket, in_buf, asio::transfer_exactly(message_header_length),
                         std::bind(&Connection::read_callback, self, _1, _2));
    }
}

void Connection::read_callback(const asio::error_code& ec, size_t num)
{
    if (!ec) {
        std::istream is(&in_buf);

        //size_t idx = line.find(' ');
        //MessageType type = parse_message_type(line.substr(0, idx));

        uint8_t net_type;
        uint16_t bytes;

        is.read(reinterpret_cast<char *>(&net_type), sizeof net_type);
        is.read(reinterpret_cast<char *>(&bytes), sizeof bytes);

        MessageType type = static_cast<MessageType>(net_type);
        bytes = ntohs(bytes);

        std::function<void(std::istream&)> read_func;
        auto self = shared_from_this();

        switch (type) {
        case OPEN:
            if (bytes != 8)
                return;
            read_func = std::bind(&Connection::read_open, self, _1);
            break;
        case ACCEPT:
            if (bytes != 1)
                return;
            read_func = std::bind(&Connection::read_accept, self, _1);
            break;
        case CONNECT:
        case ADD:
        case ERROR:
        case CHAT:
        case INVALID:
            return;
        }

        asio::async_read(socket, in_buf, asio::transfer_exactly(bytes),
                         std::bind(&Connection::read_buffer, self, _1, _2, read_func));
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
        asio::async_read(socket, in_buf, asio::transfer_exactly(message_header_length),
                         std::bind(&Connection::read_callback, shared_from_this(), _1, _2));
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

void Connection::send_connect(std::shared_ptr<Connection> other)
{
    // TODO IPv6 support
    uint8_t buf_arr[6];
    asio::mutable_buffer buf(buf_arr, sizeof(buf_arr));
    asio::buffer_copy(buf, asio::buffer(other->get_endpoint().address().to_v4().to_bytes()));
    uint16_t port = htons(other->get_endpoint().port());
    asio::buffer_copy(buf + 4, asio::buffer(&port, sizeof(port)));
    queue_write_message(CONNECT, buf);
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
        send_connect(other);
        other->send_connect(shared_from_this());
    }
    requested_from = std::weak_ptr<Connection>();
}
