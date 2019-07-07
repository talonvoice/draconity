#include "transport.h"
#include <uvw.hpp>
#include <bson.h>
#include <memory>
#include <mutex>
#include <thread>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <cstring>


#define NETWORK_DEBUG true

#ifndef streq
#define streq(a, b) (!strcmp(a, b))
#endif

#define PUBLISH_TID 0

#define BROADCAST_DELAY_MS 1000

void dump_vector(std::vector<char> buffer) {
    //FIXME can't use iostream in draconity due to using gcc's `__attribute__((constructor))`
    // for (auto const& c : buffer) {
    //     std::cout << std::hex << (int) c;
    // }
    // std::cout << std::dec << std::endl;
}

class UvClient {
    public:
        UvClient(std::shared_ptr<uvw::TCPHandle> tcp) {
            this->tcp = tcp;
        }

        void onEnd(const uvw::EndEvent &, uvw::TCPHandle &client) {
            client.close();
        }

        void onData(const uvw::DataEvent &event, uvw::TCPHandle &client) {
            if (NETWORK_DEBUG) {
                printf("[+] Draconity transport: client on data %zu\n", event.length);
            }
            lock.lock();
            auto data = &event.data[0];
            recv_buffer.insert(recv_buffer.end(), data, data + event.length);
            if (NETWORK_DEBUG) dump_vector(recv_buffer);

            while (true) {
                if (msg_len == 0) {
                    if (recv_buffer.size() >= sizeof(uint32_t) * 2) {
                        uint32_t * recv_buffer_start = reinterpret_cast<uint32_t *>(recv_buffer.data());
                        msg_tid = htonl(recv_buffer_start[0]);
                        msg_len = htonl(recv_buffer_start[1]);
                        if (NETWORK_DEBUG) {
                            printf("[+] Draconity transport: read tid %" PRIu32 " and len %" PRIu32 "\n", msg_tid, msg_len);
                            dump_vector(recv_buffer);
                        }
                        recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + sizeof(uint32_t) * 2);
                    }
                }
                if (msg_len > 0 && recv_buffer.size() >= msg_len) {
                    printf("[+] Draconity transport: received message: tid=%" PRIu32 " len=%" PRIu32 "\n", msg_tid, msg_len);
                    uint8_t * buff_start = reinterpret_cast<uint8_t *>(&recv_buffer[0]);

                    char *method = NULL;
                    int32_t counter = 0;

                    bson_t *b;
                    b = bson_new_from_data(buff_start, msg_len);
                    if (!b) {
                        printf("[!] Draconity transport: length communicated by protocol did not match length embedded in BSON object!\n");
                        //TODO some kind of error handling? maybe need to free some memory?
                        return;
                    }
                    bson_iter_t iter;
                    if (bson_iter_init(&iter, b)) {
                        while (bson_iter_next(&iter)) {
                            const char *key = bson_iter_key(&iter);
                            printf("[+] Draconity transport: bson parsing found element key: \"%s\" of type %#04x\n", bson_iter_key(&iter), bson_iter_type(&iter));
                            if (streq(key, "m") && BSON_ITER_HOLDS_UTF8(&iter)) {
                                method = bson_iter_dup_utf8(&iter, NULL);
                            } else if (streq(key, "c") && BSON_ITER_HOLDS_INT32(&iter)) {
                                counter = bson_iter_int32(&iter);
                            }
                        }
                    }
                    bson_destroy(b);
                    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + msg_len);

                    bson_t reply = BSON_INITIALIZER;
                    if streq(method, "ping") {
                        printf("[+] Draconity ping/pong: recognized ping! Received counter is %" PRIi32 "\n", counter);
                        counter++;
                        printf("[+] Draconity ping/pong: sending ping back! New counter is %" PRIi32 "\n", counter);

                        BSON_APPEND_UTF8(&reply, "m", "pong");
                        BSON_APPEND_INT32 (&reply, "c", counter);
                    } else {
                        printf("[+] Draconity ping/pong: unrecognized method: '%s'\n", method);
                    }

                    uint32_t reply_length;
                    uint8_t *reply_data = bson_destroy_with_steal(&reply, true, &reply_length);
                    write_message(msg_tid, reply_data, reply_length);
                    free(reply_data);

                    msg_tid = 0;
                    msg_len = 0;
                } else {
                    break;
                }
            }

            lock.unlock();
        }

        // Write `msg_len` bytes of `msg` to the client as a published message (i.e. not in
        // response to an incoming message).
        // Callers are responsible for freeing any data pointed at by `msg` afterwards.
        void publish(const uint8_t *msg, const size_t msg_len) {
            write_message(PUBLISH_TID, msg, msg_len);
        }

    private:
        std::shared_ptr<uvw::TCPHandle> tcp;
        std::vector<char> recv_buffer;
        std::mutex lock;

        uint32_t msg_tid = 0;
        uint32_t msg_len = 0;

        // Write `msg_len` bytes of `msg` to the client using the given transaction id of `tid`.
        // Callers are responsible for freeing any data pointed at by `msg` afterwards.
        void write_message(const uint32_t tid, const uint8_t *msg, const size_t msg_len) {
            // We jump through some hoops to allocate a new chunk of memory pointed to by a
            // `unique_ptr` (and then we pass that into uvw), so that uvw takes care of freeing
            // memory once libuv is done with actually sending the data.
            size_t header_size = sizeof(uint32_t)*2;
            size_t frame_size = header_size + msg_len;
            std::unique_ptr<char[]> data_to_write = std::make_unique<char[]>(frame_size);

            uint32_t *data_as_ints = reinterpret_cast<uint32_t*>(&data_to_write.get()[0]);
            data_as_ints[0] = ntohl(tid);
            data_as_ints[1] = ntohl(msg_len);

            std::memcpy(&data_to_write.get()[header_size], msg, msg_len);

            tcp->write(std::move(data_to_write), frame_size);

            if (NETWORK_DEBUG) printf("[+] Draconity transport: sent data to client; total bytes=%zu\n", frame_size);
        }
};

class UvServer {
    public:
        UvServer();
        ~UvServer();
        void listen(const char *host, int port);
        void startBroadcasting();
        void run();

        void publish(const uint8_t *msg, size_t length);
    private:
        std::list<std::shared_ptr<UvClient>> clients;
        std::shared_ptr<uvw::Loop> loop;
        std::mutex lock;
};

UvServer::UvServer() {
    if (NETWORK_DEBUG) printf("[+] Draconity transport: creating libuv event loop\n");
    loop = uvw::Loop::create();
    if (NETWORK_DEBUG) printf("[+] Draconity transport: done creating libuv loop\n");
}

UvServer::~UvServer() {
    loop->stop();
    // TODO: figure out if we actually need to call this close
    loop->close();
}

void UvServer::listen(const char *host, int port) {
    std::shared_ptr<uvw::TCPHandle> tcp = loop->resource<uvw::TCPHandle>();

    tcp->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::TCPHandle &srv) {
        std::shared_ptr<uvw::TCPHandle> tcpClient = srv.loop().resource<uvw::TCPHandle>();
        uvw::Addr peer = tcpClient->peer();
        printf("[+] Draconity transport: accepted connection on socket %s:%u\n", peer.ip.c_str(), peer.port);

        auto client = std::make_shared<UvClient>(tcpClient);
        tcpClient->on<uvw::CloseEvent>([this, client](const uvw::CloseEvent &, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            if (NETWORK_DEBUG) printf("[+] Draconity transport: closing connection to peer %s:%u\n", peer.ip.c_str(), peer.port);
            lock.lock();
            clients.remove(client);
            lock.unlock();
        });
        tcpClient->on<uvw::ErrorEvent>([client](const uvw::ErrorEvent &event, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            printf("[+] Draconity transport: encountered network error with connection to peer %s:%u; details: code=%i name=%s\n",
                peer.ip.c_str(), peer.port, event.code(), event.name());
        });
        tcpClient->on<uvw::EndEvent>([client](const uvw::EndEvent &event, uvw::TCPHandle &tcpClient) {
            client->onEnd(event, tcpClient);
        });
        tcpClient->on<uvw::DataEvent>([client](const uvw::DataEvent &event, uvw::TCPHandle &tcpClient) {
            client->onData(event, tcpClient);
        });
        tcpClient->on<uvw::WriteEvent>([client](const uvw::WriteEvent &event, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            if (NETWORK_DEBUG) printf("[+] Draconity transport: done writing bytes to peer %s:%u\n", peer.ip.c_str(), peer.port);
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

void UvServer::startBroadcasting() {
    std::shared_ptr<uvw::TimerHandle> timer = loop->resource<uvw::TimerHandle>();

    timer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &event, uvw::TimerHandle &timer) {
        uint64_t time_now = std::chrono::milliseconds(loop->now()).count();
        if (NETWORK_DEBUG) printf("[+] Draconity time broadcaster: it's time to send a broadcast! Time is %" PRIu64 "\n", time_now);

        bson_t *b = BCON_NEW("m", BCON_UTF8("time"), "time", BCON_INT64(time_now));
        uint32_t length;
        uint8_t *msg = bson_destroy_with_steal(b, true, &length);
        publish(msg, length);
        free(msg);
    });

    timer->start(std::chrono::milliseconds(BROADCAST_DELAY_MS), std::chrono::milliseconds(BROADCAST_DELAY_MS));
}

void UvServer::run() {
    loop->run();
}

void UvServer::publish(const uint8_t *msg, const size_t length) {
    lock.lock();
    for (auto const& client : clients) {
        //FIXME: it's supposedly not safe to call anything in libuv from outside
        // the thread running the event loop - so probably we should do something like
        // auto handle = loop->resource<uvw::AsyncHandle>()
        // and then use that to signal that there's a buffer of data to be drained
        // somewhere?
        client->publish(msg, length);
    }
    if (NETWORK_DEBUG) {
        if (clients.size() == 0) {
            printf("[+] Draconity transport: no clients connected so publish message was dropped\n");
        } else {
            printf("[+] Draconity transport: published message to %zu clients\n", clients.size());
        }
    }
    lock.unlock();
}

// `started_server` is initialized in a separate thread, so we should only read it
// while `started_server_lock` is locked.
std::mutex started_server_lock;
UvServer *started_server = nullptr;

void draconity_transport_main(transport_msg_fn callback) {
    std::thread networkThread([]{
        auto port = 8000;
        auto addr = "127.0.0.1";
        printf("[+] Draconity transport: TCP server starting on %s:%i\n", addr, port);

        started_server_lock.lock();
        UvServer server;
        server.listen(addr, port);
        server.startBroadcasting();
        started_server = &server;
        started_server_lock.unlock();

        server.run();
        printf("[!] Draconity transport: TCP server finished running (should not happen!)\n");
    });
    networkThread.detach();
}

//TODO actually we don't care about this topic anymore - remove it
void draconity_transport_publish(const char *topic, uint8_t *data, uint32_t size) {
    started_server_lock.lock();
    if (started_server == nullptr) {
        if (NETWORK_DEBUG) printf("[+] Draconity publish: dropping message because server hasn't started yet\n");
    } else {
        printf("[+] Draconity publish: attempting to publish on topic '%s'\n", topic);
        started_server->publish(data, size);
    }
    started_server_lock.unlock();
    free(data);
}