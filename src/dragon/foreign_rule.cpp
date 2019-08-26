#include "foreign_rule.h"
#include "draconity.h"

bool ForeignRule::matches(drg_grammar *other_grammar, const char *other_rule) {
    std::string other_rule_str = other_rule ? other_rule : "";
    return (this->grammar == other_grammar && this->rule == other_rule_str);
};

int ForeignRule::activate() {
    const char *rule = (this->rule.size() > 0) ? this->rule.c_str() : NULL;
    return _DSXGrammar_Activate(this->grammar, this->unk1, this->unk2, rule);
};

int ForeignRule::deactivate() {
    const char *rule = (this->rule.size() > 0) ? this->rule.c_str() : NULL;
    return _DSXGrammar_Deactivate(this->grammar, this->unk1, rule);
};
