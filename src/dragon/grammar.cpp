#include <sstream>
#include "grammar.h"
#include "draconity.h"
#include "server.h"

int Grammar::disable() {
    int rc = 0;
    std::stringstream errstream;
    if ((rc =_DSXGrammar_Deactivate(this->handle, 0, this->main_rule.c_str()))) {
        errstream << "error deactivating grammar: " << rc;
        error = errstream.str();
        return rc;
    }
    this->enabled = false;
    if ((rc =_DSXGrammar_Unregister(this->handle, this->endkey))) {
        errstream << "error removing end cb: " << rc;
        error = errstream.str();
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(this->handle, this->hypokey))) {
        errstream << "error removing hypothesis cb: " << rc;
        error = errstream.str();
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(this->handle, this->beginkey))) {
        errstream << "error removing begin cb: %d" << rc;
        error = errstream.str();
        return rc;
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
