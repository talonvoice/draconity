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

#include "abstract_platform.h"
#include "transport/transport.h"
#include "server.h"
#include "draconity.h"

UvServer::UvServer(transport_msg_fn callback, std::shared_ptr<cpptoml::table> config) {
    this->config = config;
    handle_message_callback = callback;
    loop = uvw::Loop::create();

    async_invoke_handle = loop->resource<uvw::AsyncHandle>();
    async_invoke_handle->on<uvw::AsyncEvent>([this](auto &, auto &) {
        this->drain_invoke_queue();
    });
    async_invoke_handle->on<uvw::ErrorEvent>([](auto &, auto &) {
        printf("[!] draconity transport: received error event for checking invoke queue!");
    });

    this->client_nonce = 0;
    this->secret = config->get_as<std::string>("secret").value_or("");
    bool listening = false;
    // TODO: auth connections with secret?
    if (config) {
        auto sockets = config->get_table_array("socket");
        if (secret != "" && sockets) {
            for (auto socket : *sockets) {
                auto host = socket->get_as<std::string>("host").value_or("");
                auto port = socket->get_as<int>("port").value_or(0);
                if (host != "" && port > 0) {
                    printf("[+] draconity transport: binding TCP at %s:%i\n", host.c_str(), port);
                    this->listenTCP(host, port);
                    listening = true;
                }
            }
        }
        auto pipes = config->get_table_array("pipe");
        if (secret != "" && pipes) {
            for (auto pipe : *pipes) {
                auto path = pipe->get_as<std::string>("path").value_or("");
                if (path != "") {
                    printf("[+] draconity transport: binding pipe at %s\n", path.c_str());
                    // if we're on mac, this is a file and we need to expand ~/ to HOME and unlink the old socket
                    // (on windows it's a global named pipe)
#ifdef __APPLE__
                    path = Platform::expanduser(path);
                    unlink(path.c_str());
#endif
                    this->listenPipe(path);
                    listening = true;
                }
            }
        }
    }
    if (!listening) {
        printf("[!] error: no socket/pipe configured in draconity.yml, not listening for connections\n");
    }
    if (secret == "") {
        printf("[+] error: no secret configured in draconity.yml, not accepting connections\n");
    }
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

        auto client = std::make_shared<UvClient<uvw::TCPHandle>>(stream, handle_message_callback, this->secret, this->client_nonce++);
        auto baseClient = std::static_pointer_cast<UvClientBase>(client);
        stream->once<uvw::CloseEvent>([this, baseClient](auto &, auto &stream) {
            printf("[+] draconity transport: closing TCP connection to peer %s\n", peername(stream.peer()).c_str());
            clients.remove(baseClient);
            draconity->handle_disconnect(baseClient->id);
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

        auto client = std::make_shared<UvClient<uvw::PipeHandle>>(stream, handle_message_callback, this->secret, this->client_nonce++);
        auto baseClient = std::static_pointer_cast<UvClientBase>(client);
        stream->once<uvw::CloseEvent>([this, baseClient](auto &, auto &stream) {
            printf("[+] draconity transport: closing pipe connection to peer %s\n", peername(stream.peer()).c_str());
            clients.remove(baseClient);
            draconity->handle_disconnect(baseClient->id);
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

/* Publish the `msg` to a single client.

   If the client no longer exists, does nothing.
 */
void UvServer::publish_one(std::vector<uint8_t> msg, uint32_t tid, uint64_t client_id) {
    // TODO: Store clients in a map for quicker id lookup?
    invoke([this, tid, client_id, msg{std::move(msg)}] {
        for (auto const &client : clients) {
            if (client->id == client_id) {
                client->writeMessage(tid, std::move(msg));
                return;
            }
        }
    });
}

void UvServer::drain_invoke_queue() {
    lock.lock();
    // Execute the queue outside the lock so the functions in the queue can call
    // `invoke` without deadlocking.
    auto functions = std::move(invoke_queue);
    invoke_queue.clear();
    lock.unlock();
    for (auto function : functions) {
        function();
    }
}

UvServer *server = nullptr;

void draconity_transport_main(transport_msg_fn callback, std::shared_ptr<cpptoml::table> config) {
    std::thread networkThread([config, callback] {
        server = new UvServer(callback, config);
        draconity->init_pause_timer();
        server->run();
    });
    networkThread.detach();
}

void draconity_transport_publish(std::vector<uint8_t> data) {
    if (!server) return;
    server->publish(std::move(data));
}

void draconity_transport_publish_one(std::vector<uint8_t> data, uint32_t tid, uint64_t client_id) {
    if (!server) return;
    server->publish_one(std::move(data), tid, client_id);
}
