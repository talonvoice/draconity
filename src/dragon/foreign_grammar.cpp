#include "foreign_grammar.h"
#include "draconity.h"

bool ForeignGrammar::matches(drg_grammar *other_grammar, const char *other_main_rule) {
    std::string other_rule = other_main_rule ? other_main_rule : "";
    return (this->grammar == other_grammar && this->main_rule == other_rule);
}   

int ForeignGrammar::activate() {
    const char *main_rule = (this->main_rule.size() > 0) ? this->main_rule.c_str() : NULL;
    return _DSXGrammar_Activate(this->grammar, this->unk1, this->unk2, main_rule); 
};  

int ForeignGrammar::deactivate() {
    const char *main_rule = (this->main_rule.size() > 0) ? this->main_rule.c_str() : NULL;
    return _DSXGrammar_Deactivate(this->grammar, this->unk1, main_rule);
}   
