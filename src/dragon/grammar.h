#pragma once
#include <string>
#include "types.h"

class Grammar {
    public:
        Grammar(std::string name, std::string main_rule) {
            this->key = 0;
            this->name = name;
            this->main_rule = main_rule;
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
        int unload();
        std::string error;

        uintptr_t key;
        std::string name, main_rule;
        drg_grammar *handle;

        bool enabled, exclusive;
        int priority;
        std::string appname;
        unsigned int endkey, beginkey, hypokey;
    private:
};
