#ifndef connection_h_INCLUDED
#define connection_h_INCLUDED

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
    Connection(asio::ip::tcp::socket sock,
        std::map<uint64_t, std::weak_ptr<Connection>>& conns);
    ~Connection();

    // Get the IP address of the connection
    asio::ip::tcp::endpoint get_endpoint();

    // Validates and opens the connection
    void start_connection();

    // Queues a message to write, checks to make sure it's in the right format.
    void queue_write_message(MessageType type,
        const asio::streambuf::const_buffers_type& buf);

protected:
    std::weak_ptr<Connection> requested_from;

private:
    // Callback for reading messages from socket.
    void read_callback(const asio::error_code& ec, size_t num);

    // Callback for writing messages to socket.
    void write_callback(const asio::error_code& ec, size_t num);

    asio::ip::tcp::socket socket;
    asio::streambuf in_buf;
    asio::streambuf out_buf;

    uint64_t id;

    std::map<uint64_t, std::weak_ptr<Connection>>& connections;
};

#endif // connection_h_INCLUDED
