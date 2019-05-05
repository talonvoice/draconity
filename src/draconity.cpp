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
