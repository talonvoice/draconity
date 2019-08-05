#include <bson.h>
#include "transport/transport.h"

class UvClientBase {
public:
    virtual void publish(const std::vector<uint8_t> &msg) {}
    virtual ~UvClientBase() {};
};

template <typename T>
class UvClient : public UvClientBase {
public:
    UvClient(std::shared_ptr<T> stream, transport_msg_fn callback, std::string secret) {
        this->stream = stream;
        this->handle_message_callback = callback;
        this->authed = false;
        this->secret = secret;
    }

    template <typename E>
    void onDisconnect(const E &event, T &stream) {
        stream.close();
    }

    void onData(const uvw::DataEvent &event, T &stream) {
        auto data = &event.data[0];
        recv_buffer.insert(recv_buffer.end(), data, data + event.length);
        while (true) {
            if (!received_header) {
                if (recv_buffer.size() >= sizeof(MessageHeader)) {
                    uint32_t *recv_buffer_start = reinterpret_cast<uint32_t *>(recv_buffer.data());
                    received_header = std::optional(MessageHeader{
                            .tid = htonl(recv_buffer_start[0]),
                            .length = htonl(recv_buffer_start[1])});
                    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + sizeof(MessageHeader));
                } else {
                    break;
                }
            }
            if (received_header && recv_buffer.size() >= received_header->length) {
                std::vector<uint8_t> msg(recv_buffer.begin(), recv_buffer.begin() + received_header->length);
                handleMessage(msg);
                recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + received_header->length);
                received_header = {};
            } else if (received_header) {
                // we haven't got enough data to parse the body yet
                break;
            }
        }
    }

    // Write `msg` to the client as a published message (i.e. not in
    // response to an incoming message).
    void publish(const std::vector<uint8_t> &msg) override {
        writeMessage(PUBLISH_TID, msg);
    }

private:
    void handleMessage(std::vector<uint8_t> &msg) {
        bson_t *reply = nullptr;
        if (!authed) {
            reply = handleAuth(msg);
        } else {
            reply = handle_message_callback(msg);
        }
        uint32_t reply_length;
        uint8_t *reply_data = bson_destroy_with_steal(reply, true, &reply_length);
        writeMessage(received_header->tid, reply_data, reply_length);
        bson_free(reply_data);
    }

    bson_t *handleAuth(std::vector<uint8_t> &msg) {
        std::string cmd, secret;
        bson_t root;
        if (!bson_init_static(&root, &msg[0], msg.size())) {
            return BCON_NEW(
                "success", BCON_BOOL(false),
                "error",   BCON_UTF8("failed to parse BSON"));
        }
        bson_iter_t iter;
        if (bson_iter_init(&iter, &root)) {
            while (bson_iter_next(&iter)) {
                std::string key = bson_iter_key(&iter);
                if (key == "cmd" && BSON_ITER_HOLDS_UTF8(&iter)) {
                    cmd = bson_iter_utf8(&iter, NULL);
                } else if (key == "secret" && BSON_ITER_HOLDS_UTF8(&iter)) {
                    secret = bson_iter_utf8(&iter, NULL);
                }
            }
        }
        if (cmd == "auth") {
            // constant time ish compare
            if (secret.size() == this->secret.size() && this->secret != "") {
                int diff = 0;
                for (int i = 0; i < secret.size(); i++) {
                    diff |= secret[i] ^ this->secret[i];
                }
                if (diff == 0) {
                    this->authed = true;
                    return BCON_NEW("success", BCON_BOOL(true));
                }
            }
            return BCON_NEW(
                "success", BCON_BOOL(false),
                "error",   BCON_UTF8("authentication failed"));
        }
        return BCON_NEW(
            "success", BCON_BOOL(false),
            "error",   BCON_UTF8("authentication required"));
    }

    // Write `msg_len` bytes of `msg` to the client using the given transaction id of `tid`.
    // Callers are responsible for freeing any data pointed at by `msg` afterwards.
    void writeMessage(const uint32_t tid, const uint8_t *msg, size_t msg_len) {
        // We jump through some hoops to allocate a new chunk of memory pointed to by a
        // `unique_ptr` and copy our data to write into that, so that we can pass that into
        // uvw. That way we don't have to worry about `msg`'s lifetime lasting long enough:
        // uvw will take care of freeing the memory passed to it once the data is sent.
        size_t frame_size = sizeof(MessageHeader) + msg_len;
        auto data_to_write = std::make_unique<char[]>(frame_size);
        auto header = reinterpret_cast<MessageHeader *>(&data_to_write.get()[0]);
        header->tid = ntohl(tid);
        header->length = ntohl(msg_len);
        std::memcpy(&data_to_write.get()[sizeof(MessageHeader)], msg, msg_len);
        stream->write(std::move(data_to_write), frame_size);
    }

    void writeMessage(const uint32_t tid, const std::vector<uint8_t> &msg) {
        writeMessage(tid, &msg[0], msg.size());
    }

private:
    std::string secret;
    bool authed;
    transport_msg_fn handle_message_callback;
    std::shared_ptr<T> stream;
    std::vector<uint8_t> recv_buffer;

    std::optional<MessageHeader> received_header = {};
};
