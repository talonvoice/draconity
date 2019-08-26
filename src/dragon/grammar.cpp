#include <sstream>
#include <set>
#include "grammar.h"
#include "draconity.h"
#include "server.h"
#include "phrase.h"

int Grammar::enable() {
    std::stringstream errstream;
    int rc = 0;
    this->enabled = true;
    if ((rc = _DSXGrammar_RegisterEndPhraseCallback(this->handle, phrase_end, (void *)this->key, &this->endkey))) {
        errstream << "error registering end phrase callback: " << rc;

    } else if ((rc = _DSXGrammar_RegisterPhraseHypothesisCallback(this->handle, phrase_hypothesis, (void *)this->key, &this->hypokey))) {
        errstream << "error registering phrase hypothesis callback: " << rc;

    } else if ((rc = _DSXGrammar_RegisterBeginPhraseCallback(this->handle, phrase_begin, (void *)this->key, &this->beginkey))) {
        errstream << "error registering begin phrase callback: " << rc;
    }
    if (rc) {
        error = errstream.str();
    }
    return rc;
}

int Grammar::disable() {
    this->enabled = false;
    std::stringstream errstream;
    int rc = 0;
    if ((rc =_DSXGrammar_Unregister(this->handle, this->endkey))) {
        errstream << "error removing end cb: " << rc;
    } else if ((rc = _DSXGrammar_Unregister(this->handle, this->hypokey))) {
        errstream << "error removing hypothesis cb: " << rc;
    } else if ((rc = _DSXGrammar_Unregister(this->handle, this->beginkey))) {
        errstream << "error removing begin cb: %d" << rc;
    }
    if (rc) {
        error = errstream.str();
    }
    return rc;
}

int Grammar::enable_all_rules() {
    int rc = 0;
    std::stringstream errstream;
    for (auto rule : this->top_level_rules) {
        if ((rc = this->enable_rule(rule))) {
            // TODO: Continue past this error? Stack them?
            errstream << "error enabling rule \"" << rule << "\": " << error;
            error = errstream.str();
            return rc;
        }
    }
    return rc;
}

int Grammar::disable_all_rules() {
    int rc = 0;
    std::stringstream errstream;
    // Copy so we can remove from `enabled_rules` within the iterator.
    std::set<std::string> rules_to_disable (this->active_rules);
    for (auto rule : rules_to_disable) {
        if ((rc = this->disable_rule(rule))) {
            // TODO: Continue past this error? Stack them?
            errstream << "error disabling rule \"" << rule << "\": " << error;
            error = errstream.str();
            return rc;
        }
    }
    return rc;
}

int Grammar::enable_rule(std::string rule) {
    std::stringstream errstream;
    int rc = 0;
    // TODO: Ensure the grammar has this rule?
    this->active_rules.insert(rule);
    if ((rc = _DSXGrammar_Activate(this->handle, 0, false, rule.c_str()))) {
        errstream << "error activating rule: " << rc;
        error = errstream.str();
    }
    return rc;
}

int Grammar::disable_rule(std::string rule) {
    // TODO: Error handling. What if the rule isn't enabled? What if it doesn't
    // exist in the grammar?
    this->active_rules.erase(rule);
    std::stringstream errstream;
    int rc = 0;
    if ((rc =_DSXGrammar_Deactivate(this->handle, 0, rule.c_str()))) {
        errstream << "error deactivating rule: " << rc;
        error = errstream.str();
    }
    return rc;
}

int Grammar::load(void *data_buf, uint32_t data_len) {
    std::stringstream errstream;
    dsx_dataptr dp = {.data = data_buf, .size = data_len};
    int ret = _DSXEngine_LoadGrammar(_engine, 1 /*cfg*/, &dp, &this->handle);
    if (ret > 0) {
        errstream << "error loading grammar: " << ret;
        error = errstream.str();
        return ret;
    }
    return 0;
}

int Grammar::unload() {
    int rc = 0;
    if (this->enabled) {
        if ((rc = this->disable())) {
            draconity_logf("error during grammar unload: %s", error.c_str());
        }
    }
    rc = _DSXGrammar_Destroy(this->handle);

    draconity->keylock.lock();
    draconity->grammars.erase(this->name);
    // Don't erase the key outright yet - we make it null for now to avoid race
    // conditions. See comment in `g.load` for full explanation.
    draconity->gkeys[this->key] = NULL;

    reusekey *reuse = new reusekey;
    reuse->key = this->key;
    reuse->ts = bson_get_monotonic_time();
    reuse->serial = draconity->serial;
    draconity->gkfree.push_back(reuse);
    draconity->keylock.unlock();
    return rc;
}
