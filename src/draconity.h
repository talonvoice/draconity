#ifndef draconity_H
#define draconity_H

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <cstring>
#include <uvw.hpp>

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
    void init_pause_timer();
    void sync_state();
    void clear_client_state(uint64_t client_id);
    void set_shadow_grammar(std::string name, GrammarState &shadow_grammar);
    void set_shadow_words(uint64_t client_id, uint32_t tid, std::set<std::string> &words);
    std::shared_ptr<Grammar> get_grammar(uintptr_t key);
    void handle_pause(uint64_t token);
    void handle_disconnect(uint64_t client_id);
    void client_unpause(uint64_t client_id, uint64_t token);
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

    void do_unpause();
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
    // TODO: Remove
    std::mutex dragon_lock;
    bool dragon_enabled;

    std::string engine_name;
    std::shared_ptr<cpptoml::table> config;
    std::mutex mimic_lock;
    // Each pair holds a transaction's info - <client_id, tid>
    std::queue<std::pair<uint64_t, uint32_t>> mimic_queue;
    drg_engine *engine;

    // loaded from the config
    int timeout;
    int timeout_incomplete;
    bool prevent_wake;
    // Token supplied to the pause callback. Also encodes whether Dragon is
    // paused - Dragon will never supply a token of 0, so we set this to 0 when
    // Dragon is unpaused.
    uint64_t pause_token;
private:
    uint64_t pause_timeout;  // Time in ms to wait before we force unpause.
    std::set<uint64_t> pause_clients; // Clients that haven't unpaused yet.
    std::shared_ptr<uvw::TimerHandle> pause_timer;
};

#define draconity (Draconity::shared())
#define _engine draconity->engine

#define DLAPI extern
#include "api.h"

int draconity_set_param(const char *key, const char *value);
void draconity_set_default_params();

#endif
