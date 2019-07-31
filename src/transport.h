#include <bson.h>

typedef bson_t *(*transport_msg_fn)(const uint8_t *msg, uint32_t size);
extern void draconity_transport_main(transport_msg_fn callback);
extern void draconity_transport_publish(uint8_t *data, uint32_t size);
