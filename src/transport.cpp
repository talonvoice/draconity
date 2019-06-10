#include "transport.h"
// #include <iostream>
#include <uvw.hpp>
#include <bson.h>
#include <memory>
#include <mutex>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>


#define NETWORK_DEBUG true

#ifndef streq
#define streq(a, b) (!strcmp(a, b))
#endif

#define PUBLISH_TID 0

#define BROADCAST_DELAY_MS 1000

void dump_vector(std::vector<char> buffer) {
    //FIXME
    // for (auto const& c : buffer) {
    //     std::cout << std::hex << (int) c;
    // }
    // std::cout << std::dec << std::endl;
}

class UvClient {
    public:
        UvClient(std::shared_ptr<uvw::TCPHandle> tcp) {
            // TODO: would prefer to have a pointer to a function just for sending X bytes
            // But my C++ foo is not strong enough yet to be sure whether that is safe.
            // So for now let's stash the whole TCPHandle.
            this->tcp = tcp;
        }

        void onEnd(const uvw::EndEvent &, uvw::TCPHandle &client) {
            client.close();
        }

        void onData(const uvw::DataEvent &event, uvw::TCPHandle &client) {
            if (NETWORK_DEBUG) {
                printf("[+] Draconity client on data %zu\n", event.length);
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
                            printf("[+] Draconity read tid %" PRIu32 " and len %" PRIu32 "\n", msg_tid, msg_len);
                            dump_vector(recv_buffer);
                        }
                        recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + sizeof(uint32_t) * 2);
                    }
                }
                if (msg_len > 0 && recv_buffer.size() >= msg_len) {
                    printf("[+] Draconity received message: tid=%" PRIu32 " len=%" PRIu32 "\n", msg_tid, msg_len);
                    uint8_t * buff_start = reinterpret_cast<uint8_t *>(&recv_buffer[0]);

                    char *method = NULL;
                    int32_t counter = 0;

                    bson_t *b;
                    b = bson_new_from_data(buff_start, msg_len);
                    if (!b) {
                        fprintf(stderr, "The specified length embedded in <my_data> did not match "
                                            "<my_data_len>\n");
                        //TODO some kind of error handling?
                        return;
                    }
                    bson_iter_t iter;
                    if (bson_iter_init(&iter, b)) {
                        while (bson_iter_next(&iter)) {
                            const char *key = bson_iter_key(&iter);
                            printf("[+] Draconity bson parsing found element key: \"%s\" of type %#04x\n", bson_iter_key(&iter), bson_iter_type(&iter));
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
                        printf("[+] Draconity recognized ping! Received counter is %" PRIi32 "\n", counter);
                        counter++;
                        printf("[+] Draconity sending ping back! New counter is %" PRIi32 "\n", counter);

                        BSON_APPEND_UTF8(&reply, "m", "pong");
                        BSON_APPEND_INT32 (&reply, "c", counter);
                    } else {
                        printf("[+] Draconity unrecognized method: '%s'\n", method);
                    }

                    uint32_t reply_length;
                    uint8_t *reply_data = bson_destroy_with_steal(&reply, true, &reply_length);
                    write_message(msg_tid, reply_data, reply_length);

                    msg_tid = 0;
                    msg_len = 0;
                } else {
                    break;
                }
            }

            lock.unlock();
        }

        void publish(uint8_t *msg, size_t length) {
            write_message(PUBLISH_TID, msg, length);
        }

    private:
        std::shared_ptr<uvw::TCPHandle> tcp;
        std::vector<char> recv_buffer;
        std::mutex lock;

        uint32_t msg_tid = 0;
        uint32_t msg_len = 0;

        void write_message(uint32_t tid, uint8_t *msg, size_t length) {
            uint32_t tid_network = ntohl(tid);
            uint32_t length_network = ntohl(length);
            tcp->write(reinterpret_cast<char *>(&tid_network), sizeof(uint32_t));
            tcp->write(reinterpret_cast<char *>(&length_network), sizeof(uint32_t));
            tcp->write(reinterpret_cast<char *>(msg), length);
            if (NETWORK_DEBUG) printf("[+] Draconity sent data to client; total bytes=%zu\n", sizeof(uint32_t) * 2 + length);
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
    if (NETWORK_DEBUG) fprintf(stderr, "%s", "[+] Draconity creating libuv event loop\n");
    loop = uvw::Loop::create();
    if (NETWORK_DEBUG) fprintf(stderr, "%s", "[+] Draconity done creating libuv loop\n");
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
        printf("[+] Draconity accepted connection on socket %s:%u\n", peer.ip.c_str(), peer.port);

        auto client = std::make_shared<UvClient>(tcpClient);
        tcpClient->on<uvw::CloseEvent>([this, client](const uvw::CloseEvent &, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            if (NETWORK_DEBUG) printf("[+] Draconity closing connection to peer %s:%u\n", peer.ip.c_str(), peer.port);
            lock.lock();
            clients.remove(client);
            lock.unlock();
        });
        tcpClient->on<uvw::ErrorEvent>([client](const uvw::ErrorEvent &event, uvw::TCPHandle &tcpClient) {
            uvw::Addr peer = tcpClient.peer();
            printf("[+] Draconity encountered network error with connection to peer %s:%u; details: code=%i name=%s\n",
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
            if (NETWORK_DEBUG) printf("[+] Draconity done writing bytes to peer %s:%u\n", peer.ip.c_str(), peer.port);
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
        if (NETWORK_DEBUG) printf("[+] Draconity sending broadcast happened! Time is %" PRIu64 "\n", time_now);

        for (auto const& client : clients) {
            bson_t *b = BCON_NEW("m", BCON_UTF8("time"), "time", BCON_INT64(time_now));
            uint32_t length;
            uint8_t *msg = bson_destroy_with_steal(b, true, &length);
            client->publish(msg, length);
        }
    });

    timer->start(std::chrono::milliseconds(BROADCAST_DELAY_MS), std::chrono::milliseconds(BROADCAST_DELAY_MS));
}

void UvServer::run() {
    loop->run();
}

void draconity_transport_main(transport_msg_fn cb, const char *name) {
    auto port = 8000;
    auto addr = "127.0.0.1";
    printf("[+] Server starting on %s:%i\n", addr, port);
    UvServer server;
    server.listen(addr, port);
    server.startBroadcasting();

    //TODO move below to a thread or something so it doesn't block the rest of draconity init
    server.run();
    printf("[+] Server done running (should not happen)\n");
}

void draconity_transport_publish(const char *topic, uint8_t *data, uint32_t size) {
    //TODO implement this
}