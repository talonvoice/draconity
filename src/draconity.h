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

/* Holds a desired state for the vocabulary. */
struct WordState {
    std::set<std::string> words;
    int last_tid;
    bool synced;
};

class Draconity {
public:
    static Draconity *shared();

    std::string set_dragon_enabled(bool enabled);
    void sync_state();
    void clear_client_state(uint64_t client_id);
    void set_shadow_grammar(std::string name, GrammarState &shadow_grammar);
    void set_shadow_words(uint64_t client_id, uint32_t tid, std::set<std::string> &words);
    std::shared_ptr<Grammar> get_grammar(uintptr_t key);
private:
    Draconity();
    Draconity(const Draconity &);
    Draconity& operator=(const Draconity &);

    void sync_words();
    void sync_grammars();
    void handle_word_failures(std::list<std::unordered_map<std::string, std::string>> &errors);
    void set_words(std::set<std::string> &new_words,
                   std::list<std::unordered_map<std::string, std::string>> &errors);
    void remove_grammar(std::string name, std::shared_ptr<Grammar> &grammar);
public:
    std::unordered_map<std::string, std::shared_ptr<Grammar>> grammars;
    std::unordered_map<std::string, GrammarState> shadow_grammars;

    std::set<std::string> loaded_words;
    std::unordered_map<uint64_t, WordState> shadow_words;

    const char *micstate;
    bool ready;
    uint64_t start_ts;

    std::list<ForeignRule *> dragon_rules;
    // Locks the entire shadow state
    std::mutex shadow_lock;
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
