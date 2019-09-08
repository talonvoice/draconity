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
    Grammar(std::string name) {
            this->name = name;
            this->errors = {};
            this->key = 0;
            this->handle = nullptr;
            this->enabled = false;
            this->priority = 0;
            this->endkey = 0;
            this->beginkey = 0;
            this->hypokey = 0;
        };

        void record_error(std::string type, std::string msg, int rc, std::string name);

        std::list<std::unordered_map<std::string, std::string>> errors;

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
