#include <sstream>
#include <string>
#include <stdio.h>

#include "draconity.h"
#include "abstract_platform.h"
#include "phrase.h"


#define align4(len) ((len + 4) & ~3)

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
    dragon_enabled = false;
    mimic_success = false;
    engine = NULL;

    auto config_path = Platform::expanduser("~/.talon/draconity.toml");
    config = cpptoml::parse_file(config_path);
    if (config) {
        auto logfile = *config->get_as<std::string>("logfile");
        if (logfile != "") {
            logfile = Platform::expanduser(logfile);
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

std::string Draconity::set_dragon_enabled(bool enabled) {
    std::stringstream errstream;
    std::string errmsg;
    this->dragon_lock.lock();
    if (enabled != this->dragon_enabled) {
        for (ForeignRule *fg : this->dragon_rules) {
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

int unload_grammar(std::shared_ptr<Grammar> &grammar) {
    int rc;
    // Unregister callbacks before unloading.
    if ((rc =_DSXGrammar_Unregister(grammar->handle, grammar->endkey))) {
        printf("[!] error unregistering grammar: %d\n", rc);
        return rc;
    } else if ((rc = _DSXGrammar_Unregister(grammar->handle, grammar->hypokey))) {
        printf("[!] error removing hypothesis cb: %d\n", rc);
        return rc;
    } else if ((rc = _DSXGrammar_Unregister(grammar->handle, grammar->beginkey))) {
        printf("[!] error removing begin cb: %d\n", rc);
        return rc;
    } else if ((rc = _DSXGrammar_Destroy(grammar->handle))) {
        printf("[!] error destroying grammar: %d\n", rc);
        return rc;
    }
    grammar->state.active_rules.clear();
    grammar->state.lists.clear();
    grammar->state.blob = {};
    grammar->state.unload = true;
    grammar->enabled = false;
    return 0;

}

int load_grammar(std::shared_ptr<Grammar> &grammar, std::vector<uint8_t> &blob) {
    int rc;
    void *grammar_key = (void *)grammar.get();
    dsx_dataptr blob_dp = {.data = blob.data(),
                           .size = blob.size()};
    if ((rc = _DSXEngine_LoadGrammar(_engine, 1 /* cfg */, &blob_dp, &grammar->handle))) {
        grammar->record_error("grammar", "error loading grammar", rc, grammar->name);
        return rc;
    }
    grammar->state.blob = std::move(blob);

    // Now register callbacks
    if ((rc = _DSXGrammar_RegisterEndPhraseCallback(grammar->handle, phrase_end, grammar_key, &grammar->endkey))) {
        grammar->record_error("grammar", "error registering end phrase callback", rc, grammar->name);
        return rc;
    }
    if ((rc = _DSXGrammar_RegisterPhraseHypothesisCallback(grammar->handle, phrase_hypothesis, grammar_key, &grammar->hypokey))) {
        grammar->record_error("grammar", "error registering phrase hypothesis callback", rc, grammar->name);
        return rc;
    }
    if ((rc = _DSXGrammar_RegisterBeginPhraseCallback(grammar->handle, phrase_begin, grammar_key, &grammar->beginkey))) {
        grammar->record_error("grammar", "error registering begin phrase callback", rc, grammar->name);
        return rc;
    }
    grammar->enabled = true;
    return 0;
}

void activate_rule(std::shared_ptr<Grammar> &grammar, std::string &rule) {
    int rc = _DSXGrammar_Activate(grammar->handle, 0, false, rule.c_str());
    if (rc) {
        grammar->record_error("rule", "error activating rule", rc, rule);
        return;
    }
    grammar->state.active_rules.insert(rule);
}

void deactivate_rule(std::shared_ptr<Grammar> &grammar, std::string &rule) {
    int rc = _DSXGrammar_Deactivate(grammar->handle, 0, rule.c_str());
    if (rc) {
        grammar->record_error("rule", "error deactivating rule", rc, rule);
        return;
    }
    grammar->state.active_rules.erase(rule);
}

void sync_rules(std::shared_ptr<Grammar> &grammar, GrammarState &shadow_state) {
    std::set<std::string> rules_to_enable = {};
    std::set<std::string> rules_to_disable = {};

    std::set<std::string> &shadow_rules = shadow_state.active_rules;
    std::set<std::string> &live_rules = grammar->state.active_rules;
    // Get rules to enable
    std::set_difference(shadow_rules.begin(), shadow_rules.end(),
                        live_rules.begin(), live_rules.end(),
                        std::inserter(rules_to_enable, rules_to_enable.end()));
    // Get rules to disable
    std::set_difference(live_rules.begin(), live_rules.end(),
                        shadow_rules.begin(), shadow_rules.end(),
                        std::inserter(rules_to_disable, rules_to_disable.end()));

    for (auto rule : rules_to_enable) {
        activate_rule(grammar, rule);
    }
    for (auto rule : rules_to_disable) {
        deactivate_rule(grammar, rule);
    }
}

void set_list(std::shared_ptr<Grammar> &grammar, const std::string &name, std::set<std::string> &list) {
    // List has to be passed as a dsx_dataptr - we need to construct one.
    dsx_dataptr dataptr = {.data = NULL, .size = 0};

    // Establish the dataptr's size first.
    for (auto &word : list) {
        int length = strlen(word.c_str());
        dataptr.size += sizeof(dsx_id) + align4(length);
    }

    // Now we have the size, allocate memory and populate it.
    dataptr.data = calloc(1, dataptr.size);
    uint8_t *pos = (uint8_t *)dataptr.data;
    for (auto word : list) {
        dsx_id *ent = (dsx_id *)pos;
        const char *word_cstr = word.c_str();
        uint32_t length = strlen(word_cstr);
        ent->size = sizeof(dsx_id) + align4(length);
        memcpy(ent->name, word_cstr, length);
        pos += ent->size;
    }

    // Now we can pass the list to Dragon.
    int rc = _DSXGrammar_SetList(grammar->handle, name.c_str(), &dataptr);
    if (rc) {
        grammar->record_error("list", "error setting list", rc, name);
        return;
    }
    // Only set our grammar's list when Dragon's list was set successfully.
    grammar->state.lists[name] = std::move(list);
}

void sync_lists(std::shared_ptr<Grammar> &grammar, GrammarState &shadow_state) {
    for (auto &list_pair : shadow_state.lists) {
        set_list(grammar, list_pair.first, list_pair.second);
    }
}

void sync_grammar(std::shared_ptr<Grammar> &grammar, GrammarState &shadow_state) {
    // This is where we'll accumulate errors to send to the client if things go
    // wrong - start with clean slate.
    grammar->errors.clear();

    if (grammar->state.blob != shadow_state.blob) {
        if (grammar->enabled) {
            // To replace an active blob, we have to reload the whole grammar.
            unload_grammar(grammar);
        }
        if (load_grammar(grammar, shadow_state.blob)) {
            // If the grammar failed to load, don't bother loading rules.
            return;
        }
    }

    if (grammar->state.active_rules != shadow_state.active_rules) {
        sync_rules(grammar, shadow_state);
    }
    if (grammar->state.lists != shadow_state.lists) {
        sync_lists(grammar, shadow_state);
    }
    // TODO: Sync exclusivity
}

/* Send the result of a g.set operation to the client. */
void publish_gset_response(std::string &name, std::shared_ptr<Grammar> &grammar) {
    // TODO: gset response.
    printf("[!] WARNING: g.set response publishing not implemented yet.\n");
    if (grammar->errors.empty()) {
        printf("[+] grammar loaded without errors\n");
    } else {
        printf("[+] gset errors occurred\n");
    }
}

/* Push the shadow state into Dragon - make it live. */
void Draconity::sync_state() {
    this->dragon_lock.lock();
    for (auto &pair : this->shadow_grammars) {
        std::string name = pair.first;
        auto &shadow_state = pair.second;
        auto grammar_it = this->grammars.find(name);
        std::shared_ptr<Grammar> grammar;

        if (grammar_it == this->grammars.end()) {
            // We need to have a Grammar object to synchronize on.
            grammar = std::make_shared<Grammar>(name);
            this->grammars[name] = grammar;
        } else {
            grammar = grammar_it->second;
        }

        sync_grammar(grammar, shadow_state);

        if (!grammar->errors.empty()) {
            // If any errors occurred, we unload the entire grammar and wait for
            // the user to fix it.
            if (grammar->enabled) {
                unload_grammar(grammar);
            }
            draconity->grammars.erase(name);
        }

        publish_gset_response(name, grammar);
        grammar->errors.clear();
    }
    // Wipe the shadow grammars every time we sync them. Only un-synced states
    // should be in the shadow.
    this->shadow_grammars.clear();
    this->dragon_lock.unlock();
}
