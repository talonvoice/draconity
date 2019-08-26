#pragma once
#include <string>
#include <list>
#include <set>
#include "types.h"

class Grammar {
    public:
    Grammar(std::string name, std::list<std::string> top_level_rules) {
            this->key = 0;
            this->name = name;
            this->top_level_rules = top_level_rules;
            this->active_rules = std::set<std::string>();
            this->handle = nullptr;
            this->enabled = false;
            this->exclusive = false;
            this->priority = 0;
            this->endkey = 0;
            this->beginkey = 0;
            this->hypokey = 0;
        };

        int enable();
        int disable();
        int enable_all_rules();
        int disable_all_rules();
        int enable_rule(std::string rule);
        int disable_rule(std::string rule);
        int load(void *data, uint32_t size);
        int unload();
        std::string error;

        uintptr_t key;
        std::string name;
        std::list<std::string> top_level_rules;
        std::set<std::string> active_rules;
        drg_grammar *handle;

        bool enabled, exclusive;
        int priority;
        std::string appname;
        unsigned int endkey, beginkey, hypokey;
    private:
};
