#include <sstream>
#include <string>
#include <stdio.h>
#include <bson.h>
#include <uvw.hpp>

#include "draconity.h"
#include "abstract_platform.h"
#include "phrase.h"
#include "server.h"
#include "transport/server.h"


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
    engine = NULL;
    pause_timeout = 10000;
    engine_name = "dragon";

#ifdef _WIN32
    auto config_path = Platform::expanduser("~/talon/draconity.toml");
#else
    auto config_path = Platform::expanduser("~/.talon/draconity.toml");
#endif
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
        this->timeout            = config->get_as<int>     ("timeout"           ).value_or(80);
        this->timeout_incomplete = config->get_as<int>     ("timeout_incomplete").value_or(500);
        this->prevent_wake       = config->get_as<bool>    ("prevent_wake"      ).value_or(false);
    }
    printf("[+] draconity: loaded config from %s\n", config_path.c_str());
}

// Must be called from the Uv thread
void Draconity::init_pause_timer() {
    this->pause_timer = server->loop->resource<uvw::TimerHandle>();
    this->pause_timer->on<uvw::TimerEvent>([this](const auto &, auto &handle) {
        this->do_unpause();
    });
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

std::shared_ptr<Grammar> Draconity::get_grammar(uintptr_t key) {
    for (auto &grammar_pair : this->grammars) {
        // Grammar keys correspond to the grammar's position in memory,
        // underneath the shared_ptr.
        if (key == (uintptr_t)grammar_pair.second.get()) {
            return grammar_pair.second;
        }
    }
    // Grammar couldn't be found.
    return NULL;
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
                           .size = (uint32_t)blob.size()};
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
        auto it = grammar->state.lists.find(list_pair.first);
        if (it == grammar->state.lists.end() || it->second != list_pair.second) {
            set_list(grammar, list_pair.first, list_pair.second);
        }
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
    grammar->state.client_id = shadow_state.client_id;
    grammar->state.tid = shadow_state.tid;
}

/* Append a list of grammar loading errors to a bson response */
void bson_append_errors(bson_t *response,
                        std::list<std::unordered_map<std::string, std::string>> &errors) {
    bson_t bson_errors;
    bson_t bson_error;
    BSON_APPEND_ARRAY_BEGIN(response, "errors", &bson_errors);
    char keystr[16];
    const char *key;
    int i = 0;
    for (auto const &error : errors) {
        bson_uint32_to_string(i++, &key, keystr, sizeof(keystr));
        BSON_APPEND_DOCUMENT_BEGIN(&bson_errors, key, &bson_error);
        for (auto const &pair : error) {
            BSON_APPEND_UTF8(&bson_error, pair.first.c_str(), pair.second.c_str());
        }
        bson_append_document_end(&bson_errors, &bson_error);
    }
    bson_append_array_end(response, &bson_errors);
}

/* Send the result of a g.set operation to the client.

   `status` can be one of { "success", "error", "skipped" }.

 */
void send_gset_response(const uint64_t client_id, const uint32_t tid,
                        std::string &grammar_name,
                        std::string status,
                        std::list<std::unordered_map<std::string, std::string>> &errors) {
    bson_t *response = BCON_NEW(
        "name", BCON_UTF8(grammar_name.c_str()),
        "status", BCON_UTF8(status.c_str()),
        "success", BCON_BOOL(status == "success")
    );
    bson_append_errors(response, errors);
    draconity_send("g.set", response, tid, client_id);
}

/* Empty the entire shadow state for a particular client.

   Note: this method is designed to be used when the client has disconnected, so
   it will invalidate all TIDs and won't send skips for existing shadows.

 */
void Draconity::clear_client_state(uint64_t client_id) {
    this->shadow_lock.lock();
    // Queue grammar unloads
    for (auto &grammar_pair : this->grammars) {
        std::string name = grammar_pair.first;
        auto &grammar = grammar_pair.second;
        if (grammar->state.client_id == client_id) {
            GrammarState unload_state;
            unload_state.unload = true;
            unload_state.client_id = client_id;
            this->shadow_grammars[name] = unload_state;
        }
    }
    // Queue word unloads
    auto words_it = this->shadow_words.find(client_id);
    if (words_it != this->shadow_words.end()) {
        WordState &word_state = words_it->second;
        word_state.words.clear();
        word_state.synced = false;
    }
    this->shadow_lock.unlock();
}

/* Unload & erase a live grammar. */
void Draconity::remove_grammar(std::string name, std::shared_ptr<Grammar> &grammar) {
    if (grammar->enabled) {
        unload_grammar(grammar);
    }
    this->grammars.erase(name);
}

void Draconity::sync_grammars() {
    for (auto &pair : this->shadow_grammars) {
        std::string name = pair.first;
        auto &shadow_state = pair.second;

        auto grammar_it = this->grammars.find(name);
        std::shared_ptr<Grammar> grammar;
        std::list<std::unordered_map<std::string, std::string>> errors;
        std::string operation_status;

        if (shadow_state.unload) {
            // When a grammar is flagged to unload, that's all we need to do.
            if (grammar_it != this->grammars.end()) {
                this->remove_grammar(name, grammar_it->second);
            }
            operation_status = "success";
        } else {

            if (grammar_it == this->grammars.end()) {
                // We need to have a Grammar object to synchronize on.
                grammar = std::make_shared<Grammar>(name);
                this->grammars[name] = grammar;
            } else {
                grammar = grammar_it->second;
            }

            sync_grammar(grammar, shadow_state);

            if (grammar->errors.empty()) {
                operation_status = "success";
            } else {
                operation_status = "error";
                // If any errors occurred, we unload the entire grammar and wait for
                // the user to fix it.
                this->remove_grammar(name, grammar);
            }
            errors = std::move(grammar->errors);
            grammar->errors = {};
        }

        send_gset_response(shadow_state.client_id, shadow_state.tid,
                           name, operation_status, errors);
    }
    // Only un-synced grammars should be in the shadow state.
    this->shadow_grammars.clear();
}

/* Add an error to the "w.set" error list. */
void record_word_error(std::string &word, std::string error_message,
                    std::list<std::unordered_map<std::string, std::string>> &errors) {
    std::unordered_map<std::string, std::string> error;
    error["word"] = word;
    error["message"] = error_message;
    errors.push_back(std::move(error));
}

int add_word(std::string word, std::set<std::string> &loaded_words,
             std::list<std::unordered_map<std::string, std::string>> &errors) {
    std::stringstream errstream;
    int rc;

    bool valid = false;
    const char *word_cstr = word.c_str();
    rc = _DSXEngine_ValidateWord(_engine, word_cstr, &valid);
    if (!valid) {
        if (rc == 0) {
            record_word_error(word, "error: invalid word", errors);
            return 1;
        } else {
            // Some error occured while validating the word.
            errstream << "error validating word. Return code: " << rc;
            record_word_error(word, errstream.str(), errors);
            return rc;
        }
    }
    rc = _DSXEngine_AddTemporaryWord(_engine, word_cstr, 1);
    if (rc) {
        errstream << "error adding word. Return code: " << rc;
        record_word_error(word, errstream.str(), errors);
        return rc;
    }
    // Only store the word if it was loaded successfully.
    loaded_words.insert(word);
    return 0;
}

int remove_word(std::string word, std::set<std::string> &loaded_words,
                std::list<std::unordered_map<std::string, std::string>> &errors) {
    int rc = _DSXEngine_DeleteWord(_engine, 1, word.c_str());
    if (rc) {
        std::stringstream errstream;
        errstream << "error deleting word. Return code: " << rc;
        record_word_error(word, errstream.str(), errors);
        return rc;
    }
    // Only remove the word if it was unloaded successfully.
    loaded_words.erase(word);
    return 0;
}

void Draconity::set_words(std::set<std::string> &new_words, std::list<std::unordered_map<std::string, std::string>> &errors) {
    std::set<std::string> words_to_add = {};
    std::set<std::string> words_to_remove = {};

    // Get words to add
    std::set_difference(new_words.begin(), new_words.end(),
                        this->loaded_words.begin(), this->loaded_words.end(),
                        std::inserter(words_to_add, words_to_add.end()));
    // Get words to remove
    std::set_difference(this->loaded_words.begin(), this->loaded_words.end(),
                        new_words.begin(), new_words.end(),
                        std::inserter(words_to_remove, words_to_remove.end()));
    for (auto word : words_to_add) {
        printf("[+] Adding word: %s\n", word.c_str());
        add_word(word, this->loaded_words, errors);
    }
    for (auto word : words_to_remove) {
        printf("[+] Removing word: %s\n", word.c_str());
        remove_word(word, this->loaded_words, errors);
    }
}

/* Publish the result of a "w.set" command.

   Operations are deferred. This is the result of the actual operation -
   distinct from the result of the initial API call.

 */
void send_wset_response(uint64_t client_id, uint32_t tid, std::string status,
                        std::list<std::unordered_map<std::string, std::string>> errors) {
    bson_t *response = BCON_NEW(
        "status", BCON_UTF8(status.c_str()),
        "success", BCON_BOOL(status == "success")
    );
    bson_append_errors(response, errors);
    draconity_send("w.set", response, tid, client_id);
}

void Draconity::handle_word_failures(std::list<std::unordered_map<std::string, std::string>> &errors) {
    // Deal with each client individually. Check which errors map to that
    // client, process them, then move on to the next client.
    for (auto &shadow_pair : this->shadow_words) {
        uint64_t client_id = shadow_pair.first;
        uint32_t tid = shadow_pair.second.last_tid;
        auto &shadow_words = shadow_pair.second.words;

        std::list<std::unordered_map<std::string, std::string>> client_errors;
        for (auto &error : errors) {
            std::string error_word = error["word"];
            auto shadow_word_it = shadow_words.find(error_word);
            if (shadow_word_it != shadow_words.end()) {
                client_errors.push_back(error);
                // We don't want this word to fail to load repeatedly, so remove
                // it from the shadow state.
                shadow_words.erase(error_word);
            }
        }
        if (client_errors.size() == 0) {
            send_wset_response(client_id, tid, "success", client_errors);
        } else {
            send_wset_response(client_id, tid, "error", client_errors);
        }
    }
}

void Draconity::sync_words() {
    // Errors will be accumulated in this list per-word. Later, we'll work out
    // which clients map to which error.
    std::list<std::unordered_map<std::string, std::string>> errors;

    std::set<std::string> all_shadow_words = {};
    for (auto &state_pair : this->shadow_words) {
        WordState &state = state_pair.second;
        for (std::string word : state.words) {
            all_shadow_words.insert(word);
        }
        state.synced = true;
    }
    this->set_words(all_shadow_words, errors);
    this->handle_word_failures(errors);
}

/* Push the shadow state into Dragon - make it live. */
void Draconity::sync_state() {
    this->shadow_lock.lock();
    this->sync_words();
    this->sync_grammars();
    this->shadow_lock.unlock();
}

void Draconity::set_shadow_grammar(std::string name, GrammarState &shadow_grammar) {
    this->shadow_lock.lock();
    // When an existing update exists, we replace it and notify the client
    // that it's been skipped.
    auto skipped_it = this->shadow_grammars.find(name);
    if (skipped_it != this->shadow_grammars.end()) {
        GrammarState &skipped = skipped_it->second;
        std::list<std::unordered_map<std::string, std::string>> no_errors = {};
        send_gset_response(skipped.client_id, skipped.tid, name, "skipped", no_errors);
    }
    this->shadow_grammars[name] = std::move(shadow_grammar);
    this->shadow_lock.unlock();
}

void Draconity::set_shadow_words(uint64_t client_id, uint32_t tid, std::set<std::string> &words) {
    this->shadow_lock.lock();
    auto existing_it = this->shadow_words.find(client_id);
    if ((existing_it != this->shadow_words.end()) && !existing_it->second.synced) {
        // An unsynced update exists. We need to tell the client it was skipped.
        std::list<std::unordered_map<std::string, std::string>> no_errors = {};
        send_wset_response(client_id, existing_it->second.last_tid, "skipped", no_errors);
    }
    WordState new_state;
    new_state.last_tid = tid;
    new_state.synced = false;
    new_state.words = std::move(words);
    this->shadow_words[client_id] = std::move(new_state);
    this->shadow_lock.unlock();
}

// Must be run on the Uv thread
void Draconity::do_unpause() {
    if (this->pause_token > 0 ) {
        this->pause_clients.clear();
        this->pause_timer->stop();
        _DSXEngine_Resume(_engine, this->pause_token);
        this->pause_token = 0;
    }
}

// Must be run on the Uv thread
void Draconity::client_unpause(uint64_t client_id, uint64_t token) {
    if (this->pause_token == token) {
        this->pause_clients.erase(client_id);
        if (this->pause_clients.size() == 0) {
            this->do_unpause();
        }
    }
}

void Draconity::handle_pause(uint64_t token) {
    // We run the entire pause process on the Uv thread to avoid contention.
    server->invoke([this, token] {
        this->pause_token = token;
        this->sync_state();
        if (server->clients.empty()) {
            // There are no clients to wait for so we unpause immediately.
            this->do_unpause();
        } else {
            for (auto &client : server->clients) {
                this->pause_clients.insert(client->id);
            }
            draconity_publish("paused", BCON_NEW("token", BCON_INT64(token)));
            this->pause_timer->start(uvw::TimerHandle::Time{draconity->pause_timeout},
                                     uvw::TimerHandle::Time{0});
        }
    });
}

// Must be run on uv thread.
void Draconity::handle_disconnect(uint64_t client_id) {
    // Unload everything related to a client when it disconnects.
    this->clear_client_state(client_id);
    // We might be waiting on the client to unpause.
    if (this->pause_token > 0) {
        this->sync_state();
        this->client_unpause(client_id, this->pause_token);
    }
}
