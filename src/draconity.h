#ifndef draconity_H
#define draconity_H

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>

#include "types.h"

class Grammar {
    public:
        Grammar(const char *name, const char *main_rule);
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
        ForeignGrammar();
    private:
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
        int grammar_disable(Grammar *g);
        int grammar_load(Grammar *g);
        int grammar_unload(Grammar *g);

        std::string set_dragon_enabled(bool enabled);

        std::mutex keylock;
        std::map<std::string, Grammar *> grammars;
        std::map<uintptr_t, std::string> gkeys;
        std::list<uintptr_t> gkfree;

        const char *micstate;
        bool ready;
        uint64_t start_ts, serial;

        std::map<uintptr_t, ForeignGrammar *> dragon_grammars;
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
