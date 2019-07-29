#include "draconity.h"

Draconity::Draconity() {}

std::string Draconity::gkey_to_name(uintptr_t gkey) {
    std::string ret;
    this->keylock.lock();
    auto it = this->gkeys.find(gkey);
    if (it != this->gkeys.end())
        ret = it->first;
    this->keylock.unlock();
    return ret;
}

Grammar *Draconity::grammar_get(const char *name) {
    return this->grammars[name];
}

void Draconity::grammar_set(Grammar *grammar) {
    this->grammars[grammar->name] = grammar;
}

bool ForeignGrammar::matches(drg_grammar *other_grammar, const char *other_main_rule) {
    /* Does this grammar match another?

       Both the other grammar and its main rule must be provided for the
       comparison. */

    return (this->grammar == other_grammar
            && this->main_rule_matches(other_main_rule));
}

int ForeignGrammar::activate() {
    return _DSXGrammar_Activate(this->grammar, this->unk1, this->unk2, this->main_rule);
};

int ForeignGrammar::deactivate() {
    return _DSXGrammar_Deactivate(this->grammar, this->unk1, this->main_rule);
}

bool ForeignGrammar::main_rule_matches(const char *other_main_rule) {
    /* Does this grammar's main_rule match another? */

    // Main rules match if they're both NULL, or if they're matching
    // C-strings.
    return ((this->main_rule == NULL && other_main_rule == NULL)
            || (this->main_rule && other_main_rule
                && strcmp(this->main_rule, other_main_rule) == 0));
}
