#include <bson.h>
#include "cpptoml.h"

typedef bson_t *(*transport_msg_fn)(const std::vector<uint8_t> &msg);
extern void draconity_transport_main(transport_msg_fn callback, std::shared_ptr<cpptoml::table> config);
extern void draconity_transport_publish(const std::vector<uint8_t> msg);
