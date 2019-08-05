#pragma once
#include <string>
#include "types.h"

class ForeignGrammar {
    public:
        ForeignGrammar(drg_grammar *grammar, uint64_t unk1, bool unk2, const char *main_rule) {
            this->grammar = grammar;
            this->unk1 = unk1;
            this->unk2 = unk2;
            if (main_rule) {
                this->main_rule = main_rule;
            }
        };
        
        bool matches(drg_grammar *other_grammar, const char *other_main_rule);
        int activate();
        int deactivate();
    
    private:
        drg_grammar *grammar;
        uint64_t unk1;
        bool unk2;
        std::string main_rule;
};
