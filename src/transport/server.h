#include <list>
#include <set>
#include <functional>
#include <memory>
#include <uvw.hpp>

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
    void send(std::vector<uint8_t> msg, uint32_t tid, uint64_t client_id);
    void invoke(std::function<void()> fn);
public:
    std::shared_ptr<uvw::Loop> loop;
    std::list<std::shared_ptr<UvClientBase>> clients;
private:
    std::string secret;
    void drain_invoke_queue();

    transport_msg_fn handle_message_callback;
    std::shared_ptr<cpptoml::table> config;
    std::list<std::function<void()>> invoke_queue;
    std::shared_ptr<uvw::AsyncHandle> async_invoke_handle;
    std::mutex lock; // protects access to `invoke_queue`
    int64_t client_nonce;
};


extern UvServer *server;
