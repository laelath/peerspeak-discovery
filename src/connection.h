#ifndef connection_h_INCLUDED
#define connection_h_INCLUDED

#include <chrono>
#include <map>
#include <string>

#include <asio.hpp>

enum MessageType {
    CONNECT, // Send ip to peer,         4 byte IPv4 address, 2 byte port
    OPEN,    // Open request to/from id, 8 byte uint64_t
    ACCEPT,  // Accept open request,     1 byte boolean
    ERROR,   // Response from server,    n byte string
    CHAT,    // Chat message,            n byte string
    INVALID, // Invalid message, not sent
};

// Parse a MessageType into a string, returns INVALID on a parse error.
MessageType parse_message_type(std::string type);

// Get a string from a MessageType, INVALID can't be converted.
std::string get_message_string(MessageType type);

// Class that manages a single connection, must always be a shared_ptr.
class Connection
    : public std::enable_shared_from_this<Connection> {
public:
    // Creates a connection that manages sock and is stored in conns.
    Connection(asio::io_service& io_service, asio::ip::tcp::socket sock,
               asio::ip::address &gateway, asio::ip::address &external,
               std::map<uint64_t, std::weak_ptr<Connection>>& conns);
    ~Connection();

    // Get the IP address of the connection
    asio::ip::tcp::endpoint get_endpoint();

    // Validates and opens the connection
    void start_connection();

    // Queues a message to write, checks to make sure it's in the right format.
    void queue_write_message(MessageType type,
                             const asio::const_buffer& buf);

private:
    // Initializers for connection
    void read_start_message(const asio::error_code& ec, size_t num);
    void read_start_buffer(const asio::error_code& ec, size_t num);

    // Callback for reading messages from socket.
    void read_callback(const asio::error_code& ec, size_t num);
    void read_buffer(const asio::error_code& ec, size_t num,
                     std::function<void(std::istream&)> func);

    // Callback for writing messages to socket.
    void write_callback(const asio::error_code& ec, size_t num);

    void send_connect(std::shared_ptr<Connection> other);

    void read_open(std::istream& is);
    void read_accept(std::istream& is);

    asio::ip::tcp::socket socket;
    asio::basic_waitable_timer<std::chrono::steady_clock> timer;
    asio::streambuf in_buf;

    uint64_t id;

    asio::ip::tcp::endpoint gateway, external;

    std::map<uint64_t, std::weak_ptr<Connection>>& connections;
    std::weak_ptr<Connection> requested_from;
};

#endif // connection_h_INCLUDED
