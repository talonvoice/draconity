#pragma once
#include <string>
#include <list>
#include <set>
#include <vector>
#include <unordered_map>
#include "types.h"

struct GrammarState {
    std::vector<uint8_t> blob;
    std::set<std::string> active_rules;
    std::unordered_map<std::string, std::set<std::string>> lists;
    bool unload;
};

class Grammar {
    public:
    Grammar() {
            this->key = 0;
            this->handle = nullptr;
            this->enabled = false;
            this->priority = 0;
            this->endkey = 0;
            this->beginkey = 0;
            this->hypokey = 0;
        };

        int enable();
        int disable();
        int enable_all_rules();
        int disable_all_rules();
        int enable_rule(const std::string &rule);
        int disable_rule(const std::string &rule);
        int load();
        int unload();

        GrammarState state;
        std::set<std::string> exclusive_rules;

        std::string error;

        uintptr_t key;
        std::string name;
        drg_grammar *handle;

        bool enabled;
        int priority;
        std::string appname;
        unsigned int endkey, beginkey, hypokey;
    private:
};
