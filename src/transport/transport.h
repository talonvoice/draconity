#pragma once
#include <stdint.h>

#define PUBLISH_TID 0

typedef struct __attribute__((packed)) {
    uint32_t tid, length;
} MessageHeader;
