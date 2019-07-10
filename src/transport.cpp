#include "transport.h"
#include <bson.h>
#include <memory>
#include <mutex>
#include <thread>
#include <uvw.hpp>
#define __STDC_FORMAT_MACROS
#include <cstring>
#include <inttypes.h>

// set NETWORK_DEBUG to true to see detailed transport debugging information
#define NETWORK_DEBUG true
// set NETWORK_DEBUG_DATA to true to output dumps of data sent/received
#define NETWORK_DEBUG_DATA false

#ifndef streq
#define streq(a, b) (!strcmp(a, b))
#endif

#define PUBLISH_TID 0

typedef struct __attribute__((packed)) {
    uint32_t tid, length;
} MessageHeader;

#define BROADCAST_DELAY_MS 1000

// Dump bytes as hex, courtesy of https://gist.github.com/domnikl/af00cc154e3da1c5d965
void dump_data(const char* label, char *data, size_t len) {
    if (NETWORK_DEBUG_DATA) {
        unsigned char buff[17] = {0};
        unsigned char *pc = (unsigned char *)data;

        if (label != NULL && label != nullptr) {
            printf ("%s:\n", label);
        }
        if (len == 0) {
            printf("  (zero bytes)\n");
            return;
        }

        int i;
        for (i = 0; i < len; i++) {
            // Multiple of 16 means new line (with line offset).
            if ((i % 16) == 0) {
                // Just don't print ASCII for the zeroth line.
                if (i != 0) {
                    printf("  %s\n", buff);
                }

                // Output the offset.
                printf("  %04x ", i);
            }

            // Now the hex code for the specific character.
            printf(" %02x", pc[i]);

            // And store a printable ASCII character for later.
            if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
                buff[i % 16] = '.';
            } else {
                buff[i % 16] = pc[i];
            }

            buff[(i % 16) + 1] = '\0';
        }

        // Pad out last line if not exactly 16 characters.
        while ((i % 16) != 0) {
            printf("   ");
            i++;
        }

        // And print the final ASCII bit.
        printf("  %s\n", buff);
    }
}

void dump_data(const char *label, std::vector<char> buffer) {
    dump_data(label, buffer.data(), buffer.size());
}

class UvClient {
  public:
    UvClient(std::shared_ptr<uvw::TCPHandle> tcp, transport_msg_fn callback) {
        this->tcp = tcp;
        this->handle_message_callback = callback;
    }

    void onEnd(const uvw::EndEvent &, uvw::TCPHandle &client) {
        client.close();
    }

    void onData(const uvw::DataEvent &event, uvw::TCPHandle &client) {
        if (NETWORK_DEBUG) {
            printf("[-] Draconity transport: client on data start, receiving %zu bytes into recv buffer\n",
                   event.length);
        }
        lock.lock();
        auto data = &event.data[0];
        recv_buffer.insert(recv_buffer.end(), data, data + event.length);


        while (true) {
            if (NETWORK_DEBUG) {
                printf("[-] Draconity transport: have %zu bytes in recv buffer\n", recv_buffer.size());
                dump_data("recv_buffer", recv_buffer);
            }
            if (!received_header) {
                if (recv_buffer.size() >= sizeof(MessageHeader)) {
                    uint32_t *recv_buffer_start = reinterpret_cast<uint32_t *>(recv_buffer.data());
                    received_header = std::optional(MessageHeader{
                        .tid = htonl(recv_buffer_start[0]),
                        .length = htonl(recv_buffer_start[1])});
                    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + sizeof(MessageHeader));
                    if (NETWORK_DEBUG) {
                        printf("[-] Draconity transport: read tid %" PRIu32 " and len %" PRIu32
                               ", %zu bytes left in recv buffer\n",
                               received_header->tid, received_header->length, recv_buffer.size());
                        dump_data("recv_buffer", recv_buffer);
                    }
                } else {
                    if (NETWORK_DEBUG) {
                        printf("[ ] Draconity transport: not enough bytes in recv buffer to read"
                               "%zu bytes of header\n", sizeof(MessageHeader));
                    }
                    break;
                }
            }
            if (received_header && recv_buffer.size() >= received_header->length) {
                if (NETWORK_DEBUG) {
                    printf("[-] Draconity transport: reading msg for tid=%" PRIu32
                           " by parsing %" PRIu32 " bytes of BSON from recv buffer\n",
                           received_header->tid, received_header->length);
                }

                uint8_t *buff_start = reinterpret_cast<uint8_t *>(&recv_buffer[0]);
                bson_t *reply = handle_message_callback(buff_start, received_header->length);
                recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + received_header->length);

                uint32_t reply_length;
                uint8_t *reply_data = bson_destroy_with_steal(reply, true, &reply_length);
                write_message(received_header->tid, reply_data, reply_length);
                free(reply_data);

                received_header = {};
            } else if (received_header) {
                // we haven't got enough data to parse the body yet
                if (NETWORK_DEBUG) {
                        printf("[ ] Draconity transport: not enough bytes in recv buffer to read"
                               "%" PRIi32 " bytes of message body\n", received_header->length);
                    }
                break;
            }
        }

        lock.unlock();
        if (NETWORK_DEBUG) {
            printf("[-] Draconity transport: client on data finished\n");
        }
    }

    // Write `msg_len` bytes of `msg` to the client as a published message (i.e. not in
    // response to an incoming message).
    // Callers are responsible for freeing any data pointed at by `msg` afterwards.
    void publish(const uint8_t *msg, const size_t msg_len) {
        write_message(PUBLISH_TID, msg, msg_len);
    }

  private:
    transport_msg_fn handle_message_callback;
    std::shared_ptr<uvw::TCPHandle> tcp;
    std::vector<char> recv_buffer;
    std::mutex lock;

    std::optional<MessageHeader> received_header = {};
    // MessageHeader received_header = null;

    // uint32_t msg_tid = 0;
    // uint32_t msg_len = 0;

    // Write `msg_len` bytes of `msg` to the client using the given transaction id of `tid`.
    // Callers are responsible for freeing any data pointed at by `msg` afterwards.
    void write_message(const uint32_t tid, const uint8_t *msg, const size_t msg_len) {
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

        if (NETWORK_DEBUG) {
            printf("[-] Draconity transport: queuing %zu bytes of data for sending to client\n", frame_size);
            dump_data("data_to_write", &data_to_write.get()[0], frame_size);
        }

        tcp->write(std::move(data_to_write), frame_size);
    }
};

typedef struct {
    uint8_t *msg;
    size_t length;
} PublishableMessage;

class UvServer {
  public:
    UvServer(transport_msg_fn callback);
    ~UvServer();
    void listen(const char *host, int port);
    void startBroadcasting();
    void run();

    void publish(uint8_t *msg, size_t length);

  private:
    void drain_publish_queue();

    transport_msg_fn handle_message_callback;
    std::list<std::shared_ptr<UvClient>> clients;
    std::list<PublishableMessage> publish_queue;
    std::shared_ptr<uvw::Loop> loop;
    std::shared_ptr<uvw::AsyncHandle> check_publish_queue_handle;
    std::mutex lock; // protects access to both `clients` and `publish_queue`
};

UvServer::UvServer(transport_msg_fn callback) {
    handle_message_callback = callback;
    if (NETWORK_DEBUG) {
        printf("[+] Draconity transport: creating libuv event loop machinery\n");
    }
    loop = uvw::Loop::create();

    check_publish_queue_handle = loop->resource<uvw::AsyncHandle>();
    check_publish_queue_handle->on<uvw::AsyncEvent>([this](const auto &, auto &hndl) {
        printf("[ ] Draconity transport: received async event for checking publish queue\n");
        this->drain_publish_queue();
    });
    check_publish_queue_handle->on<uvw::ErrorEvent>([](const auto &, auto &) {
        printf("[!] Draconity transport: received error event for checking publish queue!");
    });

    if (NETWORK_DEBUG) {
        printf("[+] Draconity transport: done creating libuv loop machinery\n");
    }
}

UvServer::~UvServer() {
    loop->stop();

    // async handles can keep libuv loops alive apparently: https://stackoverflow.com/a/13844553/775982
    check_publish_queue_handle->close();

    // TODO: figure out if we actually need to call this close
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
            if (NETWORK_DEBUG) {
                printf("[+] Draconity transport: closing connection to peer %s:%u\n", peer.ip.c_str(), peer.port);
            }
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
            if (NETWORK_DEBUG) {
                printf("[+] Draconity transport: done writing bytes to peer %s:%u\n", peer.ip.c_str(), peer.port);
            }
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
        if (NETWORK_DEBUG) {
            printf("[+] Draconity time broadcaster: it's time to send a broadcast! Time is %" PRIu64 "\n", time_now);
        }

        bson_t *b = BCON_NEW("cmd", BCON_UTF8("time"), "time", BCON_INT64(time_now));
        uint32_t length;
        uint8_t *msg = bson_destroy_with_steal(b, true, &length);
        publish(msg, length);
    });

    timer->start(std::chrono::milliseconds(BROADCAST_DELAY_MS), std::chrono::milliseconds(BROADCAST_DELAY_MS));
}

void UvServer::run() {
    loop->run();
}

// Send the `msg` of the length `length` to all connected clients.
// Takes ownership of the memory pointed to by `msg`.
void UvServer::publish(uint8_t *msg, const size_t length) {
    PublishableMessage m = {
        .msg = msg,
        .length = length
    };

    // Per http://docs.libuv.org/en/v1.x/design.html , it's not thread-safe to touch a libuv loop
    // outside of the thread running it, so instead we use http://docs.libuv.org/en/v1.x/async.html
    // in the form of uvw's AsyncHandle wrapper to signal to the event loop that there are messages
    // which need to be delivered.
    lock.lock();
    publish_queue.push_back(m);
    lock.unlock();
    check_publish_queue_handle->send();
}

void UvServer::drain_publish_queue() {
    lock.lock();
    size_t message_count = publish_queue.size();
    for (PublishableMessage m : publish_queue) {
        for (auto const &client : clients) {
            client->publish(m.msg, m.length);
        }
        free(m.msg);
    }
    publish_queue.clear();
    if (NETWORK_DEBUG) {
        if (clients.size() == 0) {
            printf("[+] Draconity transport: no clients connected so %zu "
                   "publish messages were dropped\n", message_count);
        } else {
            printf("[+] Draconity transport: published %zu messages to %zu clients\n",
                   message_count, clients.size());
        }
    }
    lock.unlock();
}

// `started_server` is initialized in a separate thread, so we should only read it
// while `started_server_lock` is locked.
std::mutex started_server_lock;
UvServer *started_server = nullptr;

void draconity_transport_main(transport_msg_fn callback) {
    std::thread networkThread([callback] {
        auto port = 8000;
        auto addr = "127.0.0.1";
        printf("[+] Draconity transport: TCP server starting on %s:%i\n", addr, port);

        started_server_lock.lock();
        UvServer server(callback);
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
        if (NETWORK_DEBUG) {
            printf("[+] Draconity publish: dropping message because server hasn't started yet\n");
        }
    } else {
        if (NETWORK_DEBUG) {
            printf("[+] Draconity publish: attempting to publish on topic '%s'\n", topic);
        }
        started_server->publish(data, size);
    }
    started_server_lock.unlock();
}