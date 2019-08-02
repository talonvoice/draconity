#include <bson.h>

typedef bson_t *(*transport_msg_fn)(const std::vector<uint8_t> &msg);
extern void draconity_transport_main(transport_msg_fn callback);
extern void draconity_transport_publish(const std::vector<uint8_t> msg);
