#ifndef draconity_H
#define draconity_H

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>
#include <cstring>

#include "types.h"

// Define to false to deactivate use of tack and other bits that still need porting to C++
// (Breaks draconity but is helpful for testing the rest compiles.)
#define CPP_PORT_IS_DONE true

// Define to false to disable code that relies on dragon functions existing
// (Makes draconity not so useful but it's helpful for testing other parts of the codebase.)
#define RUN_IN_DRAGON true

typedef struct {
  uint64_t key;
  uint64_t ts;
  uint64_t serial;
} reusekey;

class Grammar {
    public:
        Grammar(const char *name, const char *main_rule) {
            this->name = name;
            this->main_rule = main_rule;
        };

        int disable(std::string *errmsg);

        uintptr_t key;
        const char *name, *main_rule;
        drg_grammar *handle;

        bool enabled, exclusive;
        int priority;
        const char *appname;
        unsigned int endkey, beginkey, hypokey;
    private:
};

class ForeignGrammar {
    public:
        ForeignGrammar(drg_grammar *grammar, uint64_t unk1, bool unk2, const char *main_rule) {
            this->grammar = grammar;
            this->unk1 = unk1;
            this->unk2 = unk2;
            if (main_rule) {
                this->main_rule = strdup(main_rule);
            } else {
                this->main_rule = NULL;
            }
        };

        bool matches(drg_grammar *other_grammar, const char *other_main_rule);
        int activate();
        int deactivate();

    private:
        bool main_rule_matches(const char* other_main_rule);
        drg_grammar *grammar;
        uint64_t unk1;
        bool unk2;
        const char *main_rule;
};

class Draconity {
    public:
        Draconity();
        static Draconity *shared() {
            static Draconity *shared = NULL;
            if (!shared) shared = new Draconity();
            return shared;
        }

        std::string gkey_to_name(uintptr_t gkey);

        Grammar *grammar_get(const char *name);
        void grammar_set(Grammar *grammar);
        int grammar_enable(Grammar *g);

        std::string set_dragon_enabled(bool enabled);

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

        std::mutex mimic_lock;
        std::condition_variable mimic_cond;
        bool mimic_success;
        drg_engine *engine;
    private:
};

#define draconity (Draconity::shared())
#define _engine draconity->engine

#define DLAPI extern
#include "api.h"

int draconity_set_param(const char *key, const char *value);
void draconity_set_default_params();

#endif
