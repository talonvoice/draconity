#include <bson.h>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <inttypes.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <uvw.hpp>

#include "transport.h"

#define PUBLISH_TID 0

typedef struct __attribute__((packed)) {
    uint32_t tid, length;
} MessageHeader;

#define BROADCAST_DELAY_MS 1000

class UvClient {
  public:
    UvClient(std::shared_ptr<uvw::TCPHandle> tcp, transport_msg_fn callback) {
        this->tcp = tcp;
        this->handle_message_callback = callback;
    }

    template <typename T>
    void onDisconnect(const T &, uvw::TCPHandle &client) {
        client.close();
    }

    void onData(const uvw::DataEvent &event, uvw::TCPHandle &client) {
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
    void publish(const std::vector<uint8_t> &msg) {
        write_message(PUBLISH_TID, msg);
    }

  private:
    transport_msg_fn handle_message_callback;
    std::shared_ptr<uvw::TCPHandle> tcp;
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
        tcp->write(std::move(data_to_write), frame_size);
    }

    void write_message(const uint32_t tid, const std::vector<uint8_t> &msg) {
        write_message(tid, &msg[0], msg.size());
    }
};

class UvServer {
  public:
    UvServer(transport_msg_fn callback);
    ~UvServer();
    void listen(const char *host, int port);
    void run();

    void publish(std::vector<uint8_t> msg);
    void invoke(std::function<void()> fn);

  private:
    void drain_invoke_queue();

    transport_msg_fn handle_message_callback;
    std::list<std::shared_ptr<UvClient>> clients;
    std::list<std::function<void()>> invoke_queue;
    std::shared_ptr<uvw::Loop> loop;
    std::shared_ptr<uvw::AsyncHandle> async_invoke_handle;
    std::mutex lock; // protects access to both `clients` and `invoke_queue`
};

UvServer::UvServer(transport_msg_fn callback) {
    handle_message_callback = callback;
    loop = uvw::Loop::create();

    async_invoke_handle = loop->resource<uvw::AsyncHandle>();
    async_invoke_handle->on<uvw::AsyncEvent>([this](const auto &, auto &) {
        this->drain_invoke_queue();
    });
    async_invoke_handle->on<uvw::ErrorEvent>([](const auto &, auto &) {
        printf("[!] Draconity transport: received error event for checking invoke queue!");
    });
}

UvServer::~UvServer() {
    loop->stop();
    // async handles can keep libuv loops alive: https://stackoverflow.com/a/13844553/775982
    async_invoke_handle->close();
    loop->close();
}

void UvServer::listen(const char *host, int port) {
    std::shared_ptr<uvw::TCPHandle> tcp = loop->resource<uvw::TCPHandle>();

    tcp->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::TCPHandle &srv) {
        std::shared_ptr<uvw::TCPHandle> tcpClient = srv.loop().resource<uvw::TCPHandle>();
        uvw::Addr peer = tcpClient->peer();
        printf("[+] Draconity transport: accepted connection on socket %s:%u\n", peer.ip.c_str(), peer.port);

        auto client = std::make_shared<UvClient>(tcpClient, handle_message_callback);
        tcpClient->on<uvw::CloseEvent>([this, client](const uvw::CloseEvent &, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            printf("[+] Draconity transport: closing connection to peer %s:%u\n", peer.ip.c_str(), peer.port);
            lock.lock();
            clients.remove(client);
            lock.unlock();
        });
        tcpClient->on<uvw::ErrorEvent>([client](const uvw::ErrorEvent &event, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            printf("[+] Draconity transport: encountered network error with connection to peer %s:%u; details: code=%i name=%s\n",
                   peer.ip.c_str(), peer.port, event.code(), event.name());
            client->onDisconnect(event, tcpClient);
        });
        tcpClient->on<uvw::EndEvent>([client](const uvw::EndEvent &event, uvw::TCPHandle &tcpClient) {
            client->onDisconnect(event, tcpClient);
        });
        tcpClient->on<uvw::DataEvent>([client](const uvw::DataEvent &event, uvw::TCPHandle &tcpClient) {
            client->onData(event, tcpClient);
        });

        lock.lock();
        clients.push_back(client);
        lock.unlock();

        srv.accept(*tcpClient);
        tcpClient->read();
    });
    tcp->bind(host, port);
    tcp->listen();
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
        auto port = 8000;
        auto addr = "127.0.0.1";
        printf("[+] Draconity transport: TCP server starting on %s:%i\n", addr, port);

        server = new UvServer(callback);
        server->listen(addr, port);
        condvar.notify_one();
        server->run();
    });
    networkThread.detach();
    condvar.wait(ulock);
}

void draconity_transport_publish(std::vector<uint8_t> data) {
    server->publish(std::move(data));
}
