#ifndef draconity_H
#define draconity_H

#include <stdint.h>
#include "tack.h"

#ifdef VERBOSE
#define dprintf(...) printf(__VA_ARGS__);
#else
#define dprintf(...)
#endif

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

#define _engine draconity_engine
extern drg_engine *_engine;

#define DLAPI extern
#include "api.h"

struct state {
    pthread_t tid;
    pthread_mutex_t keylock;

    tack_t grammars;
    tack_t gkeys, gkfree;

    // dragon state
    const char *micstate;
    void *speaker;
    bool ready;
    uint64_t start_ts;
    uint64_t serial;

    tack_t dragon_grammars;
    pthread_mutex_t dragon_lock;
    bool dragon_enabled;

    pthread_mutex_t mimic_lock;
    pthread_cond_t mimic_cond;
    bool mimic_success;

    void *broker;
};

extern struct state draconity_state;

typedef struct {
    uint64_t key;
    const char *name, *main_rule;
    drg_grammar *handle;

    bool enabled, exclusive;
    int priority;
    const char *appname;
    unsigned int endkey, beginkey, hypokey;
} draconity_grammar;

typedef struct {
    drg_grammar *grammar;
    uint64_t unk1;
    bool unk2;
    const char *main_rule;
} foreign_grammar;

int draconity_set_param(const char *key, const char *value);
void draconity_set_default_params();

#endif
