#include <bson.h>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <inttypes.h>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <uvw.hpp>

#include "transport.h"

#define PUBLISH_TID 0

typedef struct __attribute__((packed)) {
    uint32_t tid, length;
} MessageHeader;

#define BROADCAST_DELAY_MS 1000

class UvClientBase {
public:
    virtual void publish(const std::vector<uint8_t> &msg) {}
};

template <typename T>
class UvClient : public UvClientBase {
  public:
    UvClient(std::shared_ptr<T> stream, transport_msg_fn callback) {
        this->stream = stream;
        this->handle_message_callback = callback;
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
                bson_t *reply = handle_message_callback(recv_buffer);
                recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + received_header->length);

                uint32_t reply_length;
                uint8_t *reply_data = bson_destroy_with_steal(reply, true, &reply_length);
                write_message(received_header->tid, reply_data, reply_length);
                bson_free(reply_data);

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
        write_message(PUBLISH_TID, msg);
    }

  private:
    transport_msg_fn handle_message_callback;
    std::shared_ptr<T> stream;
    std::vector<uint8_t> recv_buffer;

    std::optional<MessageHeader> received_header = {};
    // MessageHeader received_header = null;

    // uint32_t msg_tid = 0;
    // uint32_t msg_len = 0;

    // Write `msg_len` bytes of `msg` to the client using the given transaction id of `tid`.
    // Callers are responsible for freeing any data pointed at by `msg` afterwards.
    void write_message(const uint32_t tid, const uint8_t *msg, size_t msg_len) {
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

    void write_message(const uint32_t tid, const std::vector<uint8_t> &msg) {
        write_message(tid, &msg[0], msg.size());
    }
};

class UvServer {
  public:
    UvServer(transport_msg_fn callback);
    ~UvServer();
    void listenTCP(std::string host, int port);
    void listenPipe(std::string path);
    void run();

    void publish(std::vector<uint8_t> msg);
    void invoke(std::function<void()> fn);

  private:
    void drain_invoke_queue();

    transport_msg_fn handle_message_callback;
    std::list<std::shared_ptr<UvClientBase>> clients;
    std::list<std::function<void()>> invoke_queue;
    std::shared_ptr<uvw::Loop> loop;
    std::shared_ptr<uvw::AsyncHandle> async_invoke_handle;
    std::mutex lock; // protects access to `invoke_queue`
};

UvServer::UvServer(transport_msg_fn callback) {
    handle_message_callback = callback;
    loop = uvw::Loop::create();

    async_invoke_handle = loop->resource<uvw::AsyncHandle>();
    async_invoke_handle->on<uvw::AsyncEvent>([this](auto &, auto &) {
        this->drain_invoke_queue();
    });
    async_invoke_handle->on<uvw::ErrorEvent>([](auto &, auto &) {
        printf("[!] draconity transport: received error event for checking invoke queue!");
    });
}

UvServer::~UvServer() {
    loop->stop();
    // async handles can keep libuv loops alive: https://stackoverflow.com/a/13844553/775982
    async_invoke_handle->close();
    loop->close();
}

static std::string peername(uvw::Addr peer) {
    std::ostringstream stream;
    stream << peer.ip << ":" << peer.port;
    return stream.str();
}

static std::string peername(std::string peer) {
    return peer;
}

void UvServer::listenTCP(std::string host, int port) {
    auto resource = loop->resource<uvw::TCPHandle>();
    resource->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::TCPHandle &srv) {
        auto stream = srv.loop().resource<uvw::TCPHandle>();
        printf("[+] draconity transport: accepted TCP connection from peer %s\n", peername(stream->peer()).c_str());

        auto client = std::make_shared<UvClient<uvw::TCPHandle>>(stream, handle_message_callback);
        auto baseClient = std::static_pointer_cast<UvClientBase>(client);
        stream->once<uvw::CloseEvent>([this, baseClient](auto &, auto &stream) {
            printf("[+] draconity transport: closing TCP connection to peer %s\n", peername(stream.peer()).c_str());
            clients.remove(baseClient);
        });
        stream->once<uvw::ErrorEvent>([client](auto &event, auto &stream) {
            printf("[+] draconity transport: TCP error for peer %s: [%d] %s\n",
                   peername(stream.peer()).c_str(), event.code(), event.name());
            client->onDisconnect(event, stream);
        });
        stream->once<uvw::EndEvent>([client](auto &event, auto &stream) {
            client->onDisconnect(event, stream);
        });
        stream->on<uvw::DataEvent>([client](auto &event, auto &stream) {
            client->onData(event, stream);
        });

        clients.push_back(baseClient);
        srv.accept(*stream);
        stream->read();
    });
    resource->on<uvw::ErrorEvent>([](auto &event, auto &resource) {
        printf("[+] draconity TCP transport error[%d]: %s\n", event.code(), event.name());
    });
    resource->bind(host, port);
    resource->listen();
}

void UvServer::listenPipe(std::string path) {
    // mostly duplicated from listenTCP
    auto resource = loop->resource<uvw::PipeHandle>();
    resource->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::PipeHandle &srv) {
        auto stream = srv.loop().resource<uvw::PipeHandle>();
        printf("[+] draconity transport: accepted pipe connection from peer %s\n", peername(stream->peer()).c_str());

        auto client = std::make_shared<UvClient<uvw::PipeHandle>>(stream, handle_message_callback);
        auto baseClient = std::static_pointer_cast<UvClientBase>(client);
        stream->once<uvw::CloseEvent>([this, baseClient](auto &, auto &stream) {
            printf("[+] draconity transport: closing pipe connection to peer %s\n", peername(stream.peer()).c_str());
            clients.remove(baseClient);
        });
        stream->once<uvw::ErrorEvent>([client](auto &event, auto &stream) {
            printf("[+] draconity transport: pipe error for peer %s: [%d] %s\n",
                   peername(stream.peer()).c_str(), event.code(), event.name());
            client->onDisconnect(event, stream);
        });
        stream->once<uvw::EndEvent>([client](auto &event, auto &stream) {
            client->onDisconnect(event, stream);
        });
        stream->on<uvw::DataEvent>([client](auto &event, auto &stream) {
            client->onData(event, stream);
        });

        clients.push_back(baseClient);
        srv.accept(*stream);
        stream->read();
    });
    resource->on<uvw::ErrorEvent>([](auto &event, auto &resource) {
        printf("[+] draconity pipe transport error[%d]: %s\n", event.code(), event.name());
    });
    resource->bind(path);
    resource->listen();
}

void UvServer::run() {
    loop->run();
}

void UvServer::invoke(std::function<void()> fn) {
    // Invoke a function on the event loop's thread.
    // Per http://docs.libuv.org/en/v1.x/design.html , it's not thread-safe to touch a libuv loop
    // outside of the thread running it, so instead we use http://docs.libuv.org/en/v1.x/async.html
    // in the form of uvw's AsyncHandle wrapper to signal the event loop to call `UvServer::drain_invoke_queue()`
    lock.lock();
    invoke_queue.push_back(fn);
    lock.unlock();
    async_invoke_handle->send();
}

// Publish (TID 0) the `msg` to all connected clients.
void UvServer::publish(std::vector<uint8_t> msg) {
    invoke([this, msg{std::move(msg)}] {
        for (auto const &client : clients) {
            client->publish(msg);
        }
    });
}

void UvServer::drain_invoke_queue() {
    lock.lock();
    for (auto fn : invoke_queue) {
        fn();
    }
    invoke_queue.clear();
    lock.unlock();
}

UvServer *server = nullptr;

void draconity_transport_main(transport_msg_fn callback) {
    std::mutex lock;
    std::condition_variable condvar;
    std::unique_lock<std::mutex> ulock(lock);

    std::thread networkThread([&condvar, callback] {
        // TODO: configurable host/port/pipe
        std::string pipe;
        std::string addr;
        int port = 0;
        addr = "127.0.0.1";
        port = 38065;

        server = new UvServer(callback);
        if (addr != "") {
            printf("[+] draconity transport: binding TCP at %s:%i\n", addr.c_str(), port);
            server->listenTCP(addr, port);
        }
        if (pipe != "") {
            printf("[+] draconity transport: binding pipe at %s\n", pipe.c_str());
            // TODO: make parent directories?
            // TODO: only pretend it's a file if we're on mac?
            unlink(pipe.c_str());
            server->listenPipe(pipe);
        }
        condvar.notify_one();
        server->run();
    });
    networkThread.detach();
    condvar.wait(ulock);
}

void draconity_transport_publish(std::vector<uint8_t> data) {
    server->publish(std::move(data));
}
