#pragma once
#include <stdint.h>

typedef struct {
    void *data;
    uint32_t size;
} dsx_dataptr;

typedef struct {
    uint32_t size, id;
    char name[0];
} __attribute__((packed)) dsx_id;

typedef struct {
    uint32_t var1;
    uint32_t var2;
    uint32_t var3;
    uint32_t var4;
    uint32_t var5;
    uint32_t var6;
    uint64_t start_time;
    uint64_t end_time;
    uint32_t var7;
    uint32_t var8;
    uint32_t var9;
    uint32_t var10;
    uint32_t var11;
    uint32_t var12;
    uint32_t rule;
    uint32_t var13;
} __attribute__((packed)) dsx_word_node;

typedef struct {
    uint32_t flags, num, flags2, flags3, tag;
} __attribute__((packed)) drg_wordinfo;

typedef struct {} drg_grammar;
typedef struct {} drg_engine;
typedef struct {} drg_filesystem;
typedef struct {} drg_worditer;
typedef struct {} dsx_result;

typedef struct {
    void *var0;
    unsigned int var1;
    unsigned int flags;
    uint64_t var3;
    uint64_t var4;
    char *phrase;
    dsx_result *result;
    void *var7;
} dsx_end_phrase;

typedef struct {
    void *user;
    char *name;
} dsx_attrib;

typedef struct {
    void *user;
    uint64_t token;
} dsx_paused;

typedef struct {
    void *user;
    uint64_t flags;
} dsx_mimic;
