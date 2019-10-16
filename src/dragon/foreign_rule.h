#pragma once
#include <string>
#include "types.h"

/* Class representing an internal Dragon rule.

   Rules are referenced by name, namespaced to a particular grammar.

 */
class ForeignRule {
    public:
        ForeignRule(drg_grammar *grammar, uint64_t unk1, bool unk2, const char *rule) {
            this->grammar = grammar;
            this->unk1 = unk1;
            this->unk2 = unk2;
            if (rule) {
                this->rule = rule;
            }
        };
        
        bool matches(drg_grammar *other_grammar, const char *other_rule);
        int activate();
        int deactivate();
    
    private:
        drg_grammar *grammar;
        uint64_t unk1;
        bool unk2;
        std::string rule;
};
