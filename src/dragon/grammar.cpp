#include <sstream>
#include "grammar.h"
#include "draconity.h"
#include "server.h"
#include "phrase.h"

int Grammar::enable() {
    std::stringstream errstream;
    int rc = 0;
    if ((rc = _DSXGrammar_Activate(this->handle, 0, false, this->main_rule.c_str()))) {
        errstream << "error activating grammar: " << rc;
        error = errstream.str();
        return rc;
    }
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
    if ((rc =_DSXGrammar_Deactivate(this->handle, 0, this->main_rule.c_str()))) {
        errstream << "error deactivating grammar: " << rc;
        error = errstream.str();
        return rc;
    }
    this->enabled = true;
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
