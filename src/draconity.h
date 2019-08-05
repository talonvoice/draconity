#ifndef draconity_H
#define draconity_H

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>
#include <cstring>

#include "cpptoml.h"
#include "types.h"
#include "dragon/grammar.h"
#include "dragon/foreign_grammar.h"

typedef struct {
  uint64_t key;
  uint64_t ts;
  uint64_t serial;
} reusekey;

class Draconity {
public:
    static Draconity *shared();

    std::string gkey_to_name(uintptr_t gkey);

    Grammar *grammar_get(const char *name);
    void grammar_set(Grammar *grammar);

    std::string set_dragon_enabled(bool enabled);
private:
    Draconity();
    Draconity(const Draconity &);
    Draconity& operator=(const Draconity &);

public:
    std::mutex keylock;
    std::map<std::string, Grammar *> grammars;
    std::map<uintptr_t, Grammar *> gkeys;
    std::list<reusekey *> gkfree;

    const char *micstate;
    bool ready;
    uint64_t start_ts, serial;

    std::list<ForeignGrammar *> dragon_grammars;
    std::mutex dragon_lock;
    bool dragon_enabled;

    std::shared_ptr<cpptoml::table> config;
    std::mutex mimic_lock;
    std::condition_variable mimic_cond;
    bool mimic_success;
    drg_engine *engine;

    // loaded from the config
    int timeout;
    int timeout_incomplete;
    bool prevent_wake;
};

#define draconity (Draconity::shared())
#define _engine draconity->engine

#define DLAPI extern
#include "api.h"

int draconity_set_param(const char *key, const char *value);
void draconity_set_default_params();

#endif
