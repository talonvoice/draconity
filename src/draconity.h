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
#include "dragon/foreign_rule.h"

class Draconity {
public:
    static Draconity *shared();

    std::string set_dragon_enabled(bool enabled);
private:
    Draconity();
    Draconity(const Draconity &);
    Draconity& operator=(const Draconity &);

public:
    std::map<std::string, std::shared_ptr<Grammar>> grammars;
    std::map<std::string, GrammarState> shadow_grammars;

    const char *micstate;
    bool ready;
    uint64_t start_ts;

    std::list<ForeignRule *> dragon_rules;
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
