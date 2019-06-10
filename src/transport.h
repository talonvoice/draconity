#include <bson.h>

typedef bson_t *(*transport_msg_fn)(const uint8_t *msg, uint32_t size);
extern void draconity_transport_main(transport_msg_fn cb, const char *name);
extern void draconity_transport_publish(const char *topic, uint8_t *data, uint32_t size);
