#include <sstream>
#include <string>
#include <stdio.h>

#include "draconity.h"
#include "platform.h"

void draconity_install();
static Draconity *instance = NULL;
Draconity *Draconity::shared() {
    if (!instance) {
        instance = new Draconity();
        draconity_install();
    }
    return instance;
}

Draconity::Draconity() {
    micstate = NULL;
    ready = false;
    start_ts = 0;
    serial = 0;
    dragon_enabled = false;
    mimic_success = false;
    engine = NULL;

    auto config_path = Platform::expanduser("~/.talon/draconity.toml");
    config = cpptoml::parse_file(config_path);
    if (config) {
        auto logfile = *config->get_as<std::string>("logfile");
        if (logfile != "") {
            freopen(logfile.c_str(), "a", stdout);
            freopen(logfile.c_str(), "a", stderr);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
        // dump the config, commented out by default because it contains the secret
        if (false) {
            std::cout << "================================" << std::endl;
            std::cout << *config;
            std::cout << "================================" << std::endl;
        }
        // dragon config values
        this->timeout            = config->get_as<int> ("timeout"           ).value_or(80);
        this->timeout_incomplete = config->get_as<int> ("timeout_incomplete").value_or(500);
        this->prevent_wake       = config->get_as<bool>("prevent_wake"      ).value_or(false);
    }
    printf("[+] draconity: loaded config from %s\n", config_path.c_str());
}

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

std::string Draconity::set_dragon_enabled(bool enabled) {
    std::stringstream errstream;
    std::string errmsg;
    this->dragon_lock.lock();
    if (enabled != this->dragon_enabled) {
        for (ForeignGrammar *fg : this->dragon_grammars) {
            int rc;
            if (enabled) {
                if ((rc = fg->activate())) {
                    errstream << "error activating grammar: " << rc;
                    errmsg = errstream.str();
                    break;
                }
            } else {
                if ((rc = fg->deactivate())) {
                    errstream << "error deactivating grammar: " << rc;
                    errmsg = errstream.str();
                    break;
                }
            }
        }
        this->dragon_enabled = enabled;
    }
    this->dragon_lock.unlock();
    return "";
}

int Grammar::disable(std::string *errmsg) {
    int rc = 0;
    std::stringstream errstream;
    if ((rc =_DSXGrammar_Deactivate(this->handle, 0, this->main_rule.c_str()))) {
        errstream << "error deactivating grammar: " << rc;
        *errmsg = errstream.str();
        return rc;
    }
    this->enabled = false;
    if ((rc =_DSXGrammar_Unregister(this->handle, this->endkey))) {
        errstream << "error removing end cb: " << rc;
        *errmsg = errstream.str();
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(this->handle, this->hypokey))) {
        errstream << "error removing hypothesis cb: " << rc;
        *errmsg = errstream.str();
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(this->handle, this->beginkey))) {
        errstream << "error removing begin cb: %d" << rc;
        *errmsg = errstream.str();
        return rc;
    }
    return 0;
}

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
