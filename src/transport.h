#include <bson.h>

//FIXME probably need to define dummy versions of these functions to get this to compile
// (and later introduce real networking code of course)
typedef bson_t *(*transport_msg_fn)(const uint8_t *msg, uint32_t size);
void draconity_transport_main(transport_msg_fn cb, const char *name);
void draconity_transport_publish(const char *topic, uint8_t *data, uint32_t size);
