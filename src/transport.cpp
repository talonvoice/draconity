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

#include "platform.h"
#include "transport.h"
#include "transport/client.h"
#include "transport/transport.h"

class UvServer {
public:
    UvServer(transport_msg_fn callback, std::shared_ptr<cpptoml::table> config);
    ~UvServer();
    void listenTCP(std::string host, int port);
    void listenPipe(std::string path);
    void run();

    void publish(std::vector<uint8_t> msg);
    void invoke(std::function<void()> fn);

private:
    std::string secret;
    void drain_invoke_queue();

    transport_msg_fn handle_message_callback;
    std::shared_ptr<cpptoml::table> config;
    std::list<std::shared_ptr<UvClientBase>> clients;
    std::list<std::function<void()>> invoke_queue;
    std::shared_ptr<uvw::Loop> loop;
    std::shared_ptr<uvw::AsyncHandle> async_invoke_handle;
    std::mutex lock; // protects access to `invoke_queue`
};

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

        auto client = std::make_shared<UvClient<uvw::TCPHandle>>(stream, handle_message_callback, this->secret);
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

        auto client = std::make_shared<UvClient<uvw::PipeHandle>>(stream, handle_message_callback, this->secret);
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

void draconity_transport_main(transport_msg_fn callback, std::shared_ptr<cpptoml::table> config) {
    std::mutex lock;
    std::condition_variable condvar;
    std::unique_lock<std::mutex> ulock(lock);

    std::thread networkThread([&condvar, config, callback] {
        server = new UvServer(callback, config);
        condvar.notify_one();
        server->run();
    });
    networkThread.detach();
    condvar.wait(ulock);
}

void draconity_transport_publish(std::vector<uint8_t> data) {
    server->publish(std::move(data));
}
