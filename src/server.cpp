#include <sstream>
#include <vector>
#include <memory>

#include <bson.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "transport/transport.h"
#include "phrase.h"
#include "server.h"
#include "draconity.h"
#include "dr_time.h"

#ifndef streq
#define streq(a, b) !strcmp(a, b)
#endif

#define US  1
#define MS  (1000 * US)
#define SEC (1000 * MS)

/* Prepare a response to be pubished */
std::vector<uint8_t> prep_response(const char *topic, bson_t *obj) {
    BSON_APPEND_INT64(obj, "ts", dr_monotonic_time());
    BSON_APPEND_UTF8(obj, "topic", topic);
    uint32_t length = 0;
    uint8_t *buf = bson_destroy_with_steal(obj, true, &length);
    std::vector<uint8_t> vec;
    if (length > 0) {
        vec = std::vector(buf, buf + length);
    } else {
        vec = {};
    }
    bson_free(buf);
    return vec;
}

/* Publish a message to all clients */
void draconity_publish(const char *topic, bson_t *obj) {
    auto response = prep_response(topic, obj);
    if (!response.empty()) {
        draconity_transport_publish(std::move(response));
    }
}

/* Publish a message to a single client */
void draconity_send(const char *topic, bson_t *obj, uint32_t tid, uint64_t client_id) {
    auto response = prep_response(topic, obj);
    if (!response.empty()) {
        draconity_transport_send(std::move(response), tid, client_id);
    }
}

void draconity_logf(const char *fmt, ...) {
    char *str = NULL;
    va_list va;
    va_start(va, fmt);
    int needed = vsnprintf(NULL, 0, fmt, va);
    va_end(va);

    va_start(va, fmt);
    str = new char[needed + 1];
    vsnprintf(str, needed + 1, fmt, va);
    va_end(va);

    bson_t obj = BSON_INITIALIZER;
    BSON_APPEND_UTF8(&obj, "msg", str);
    draconity_publish("log", &obj);
    delete str;
}

static const char *micstates[] = {
    "disabled",
    "off",
    "on",
    "sleeping",
    "pause",
    "resume",
};

// Get the int code for a settable mic state name. Returns -1 if it's invalid.
static int settable_micstate_code(char* state_name) {
    // We only allow these three micstates - the others aren't very useful.
    if (streq(state_name, "off")) {
        return 1;
    } else if (streq(state_name, "on")) {
        return 2;
    } else if (streq(state_name, "sleeping")) {
        return 3;
    } else {
        return -1;
    }
}

static bson_t *success_msg() {
    return BCON_NEW("success", BCON_BOOL(true));
}

static bson_t *handle_message(uint64_t client_id, uint32_t tid, const std::vector<uint8_t> &msg) {
    std::ostringstream errstream;
    std::string errmsg = "";

    char *cmd = NULL, *name = NULL, *state = NULL;
    bool exclusive = false;
    int priority = 0, counter;
    // Dragon won't supply a pause token of 0, so 0 implies no token.
    uint64_t token = 0;
    bool has_exclusive = false, has_priority = false, has_lists = false;

    const uint8_t *data_buf = NULL, *phrase_buf = NULL, *words_buf, *active_rules_buf = NULL, *lists_buf = NULL;
    uint32_t data_len = 0, phrase_len = 0, words_len, active_rules_len = 0, lists_len = 0;

    bson_t *resp = NULL;
    bson_t root;
    if (!bson_init_static(&root, &msg[0], msg.size())) {
        errmsg = "bson init error";
        goto end;
    }
    bson_iter_t iter;
    if (bson_iter_init(&iter, &root)) {
        while (bson_iter_next(&iter)) {
            const char *key = bson_iter_key(&iter);
            if (streq(key, "cmd") && BSON_ITER_HOLDS_UTF8(&iter)) {
                cmd = bson_iter_dup_utf8(&iter, NULL);
            } else if (streq(key, "name") && BSON_ITER_HOLDS_UTF8(&iter)) {
                name = bson_iter_dup_utf8(&iter, NULL);
            } else if (streq(key, "state") && BSON_ITER_HOLDS_UTF8(&iter)) {
                state = bson_iter_dup_utf8(&iter, NULL);
            } else if (streq(key, "exclusive") && BSON_ITER_HOLDS_BOOL(&iter)) {
                exclusive = bson_iter_bool(&iter);
                has_exclusive = true;
            } else if (streq(key, "priority") && BSON_ITER_HOLDS_INT32(&iter)) {
                priority = bson_iter_int32(&iter);
                has_priority = true;
            } else if (streq(key, "token") && BSON_ITER_HOLDS_INT64(&iter)) {
                token = bson_iter_int64(&iter);
            } else if (streq(key, "active_rules") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &active_rules_len, &active_rules_buf);
            } else if (streq(key, "lists") && BSON_ITER_HOLDS_DOCUMENT(&iter)) {
                bson_iter_document(&iter, &lists_len, &lists_buf);
                has_lists = true;
            } else if (streq(key, "phrase") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &phrase_len, &phrase_buf);
            } else if (streq(key, "words") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &words_len, &words_buf);
            } else if (streq(key, "data") && BSON_ITER_HOLDS_BINARY(&iter)) {
                bson_iter_binary(&iter, NULL, &data_len, &data_buf);
            }
        }
    }
    if (!cmd) {
        errmsg = "missing or broken cmd field";
        goto end;
    }
    if (cmd[0] == 'w') {
        if (!draconity->ready) goto not_ready;
        if (!_DSXEngine_EnumWords ||
                !_DSXWordEnum_Next ||
                !_DSXEngine_AddWord ||
                !_DSXEngine_DeleteWord ||
                !_DSXEngine_ValidateWord) {
            errmsg = "engine does not support vocabulary editing";
            goto end;
        }
        if (streq(cmd, "w.list")) {
            const char *index;
            bson_t *response = bson_new();
            char keystr[16];
            int i = 0;
            bson_t words;
            BSON_APPEND_ARRAY_BEGIN(response, "words", &words);
            bool all_good = true;
            drg_worditer *wenum = _DSXEngine_EnumWords(_engine, 1);
            if (!wenum) {
                bson_free(response);
                errmsg = "word iterator is null";
                goto end;
            }
            uint32_t max_size = 0x200000, max_words = 50000;
            bson_reserve_buffer(response, max_size);
            char *buf = new char[max_size];
            int err = 0;
            while (1) {
                uint32_t size = 0, count = 0;
                err = _DSXWordEnum_Next(wenum, max_words, buf, &count, max_size, &size);
                if ((err != 0 && err != -1) || (size > max_size)) {
                    errmsg = "word iteration failed";
                    break;
                }
                if (!size) break;
                char *pos = buf;
                for (int j = 0; j < count; j++) {
                    pos += 20; // there's a wordinfo struct here I think
                    size_t idx_size = bson_uint32_to_string(i++, &index, keystr, sizeof(keystr));
                    size_t word_size = strlen(pos);
                    bson_append_utf8(&words, index, idx_size, pos, word_size);
                    pos += (word_size + 4) & ~3;
                }
            }
            delete []buf;
            if (errmsg.size() > 0) {
                bson_free(response);
                goto end;
            }
            bson_append_array_end(response, &words);
            BSON_APPEND_BOOL(response, "success", all_good);
            resp = response;
            goto end;
        } else if (streq(cmd, "w.set")) {
            std::set<std::string> shadow_words = {};
            if (!words_buf || !words_len) {
                errmsg = "missing or broken words field";
                goto end;
            }
            if (!bson_iter_init_from_data(&iter, words_buf, words_len)) {
                errmsg = "word iter failed";
                goto end;
            }
            while (bson_iter_next(&iter)) {
                if (!BSON_ITER_HOLDS_UTF8(&iter)) {
                    errmsg = "words contains non-string value";
                    goto end;
                }
                const char *word = bson_iter_utf8(&iter, NULL);
                shadow_words.insert(word);
            }

            draconity->set_shadow_words(client_id, tid, shadow_words);

            // Response will be sent when update is synced (or discarded).
            goto no_response;
        } else {
            goto unsupported_command;
        }
    } else if (streq(cmd, "ready")) {
        draconity_ready();
        resp = success_msg();
    } else if (cmd[0] == 'g') {
        // TODO: If not ready, shouldn't we just push the update anyway?
        if (!draconity->ready) goto not_ready;

        GrammarState shadow_grammar;
        shadow_grammar.tid = tid;
        shadow_grammar.client_id = client_id;

        // Decode name
        if (!name) {
            errmsg = "no name";
            goto end;
        }

        if (streq(cmd, "g.set")) {
            if (!data_buf || !data_len) {
                errmsg = "missing or broken data field";
                goto end;
            }
            shadow_grammar.blob = std::move(std::vector<uint8_t>(data_buf, data_buf + data_len));

            // Decode "rules"
            if (!active_rules_buf || !active_rules_len) {
                errmsg = "missing or broken active_rules field";
                goto end;
            }
            bson_iter_t rules_iter;
            if (!bson_iter_init_from_data(&rules_iter, active_rules_buf, active_rules_len)) {
                errmsg = "active_rules iter failed";
                goto end;
            }
            while (bson_iter_next(&rules_iter)) {
                if (!BSON_ITER_HOLDS_UTF8(&rules_iter)) {
                    errmsg = "active_rules array contained non-string element";
                    goto end;
                }
                const char *rule = bson_iter_utf8(&rules_iter, NULL);
                shadow_grammar.active_rules.insert(rule);
            }

            // Decode "lists"
            if (has_lists) {
                if (!lists_buf || !lists_len) {
                    errmsg = "missing or broken lists field";
                    goto end;
                }
                bson_iter_t lists_iter;
                if (!bson_iter_init_from_data(&lists_iter, lists_buf, lists_len)) {
                    errmsg = "lists iter failed";
                    goto end;
                }
                while (bson_iter_next(&lists_iter)) {
                    std::string list_name = bson_iter_key(&lists_iter);
                    bson_iter_t this_list_iter;
                    if (!BSON_ITER_HOLDS_ARRAY(&lists_iter)) {
                        errstream << "value field for list \"" << list_name << "\" contains non-array value";
                        errmsg = errstream.str();
                        goto end;
                    }
                    if (!bson_iter_recurse(&lists_iter, &this_list_iter)) {
                        errstream << "error recursing into list " << list_name;
                        errmsg = errstream.str();
                        goto end;
                    }
                    std::set<std::string> list_contents;
                    // Pull each element out of the list, into a vector.
                    while (bson_iter_next(&this_list_iter)) {
                        if (!BSON_ITER_HOLDS_UTF8(&this_list_iter)) {
                            errstream << "an element in list \"" << list_name << "\" is not a string";
                            errmsg = errstream.str();
                            goto end;
                        }
                        std::string element_str = bson_iter_utf8(&this_list_iter, NULL);
                        list_contents.insert(element_str);
                    }
                    shadow_grammar.lists[list_name] = std::move(list_contents);
                }
            }
            shadow_grammar.unload = false;

            draconity->set_shadow_grammar(name, shadow_grammar);
        } else if (streq(cmd, "g.unload")) {
            shadow_grammar.unload = true;
            draconity->set_shadow_grammar(name, shadow_grammar);
        } else {
            goto unsupported_command;
        }
        if (draconity->pause_token != 0) {
            // When Dragon is paused, we sync immediately (this allows the
            // client to correct errors before unpausing).
            draconity->sync_state();
        }
        // Response will be sent when update is synced (or discarded).
        goto no_response;
    } else if (streq(cmd, "mic.set_state")) {
        if (!state) {
            errmsg = "missing or broken state field";
            goto end;
        }
        int micstate_code = settable_micstate_code(state);
        if (micstate_code == -1) {
            errmsg = "invalid mic state";
            goto end;
        }
        int rc = _DSXEngine_SetMicState(_engine, micstate_code, 0, 0);
        // An rc of -1 means we're already in the target mic state - that's
        // fine.
        if (rc && rc != -1) {
            errstream << "error setting mic state: " << rc;
            errmsg = errstream.str();
            goto end;
        }
        resp = success_msg();
        goto end;
    } else if (streq(cmd, "unpause")) {
        if (token == 0 || token != draconity->pause_token) {
            errmsg = "missing or broken pause token";
            goto end;
        }
        draconity->client_unpause(client_id, token);
        resp = success_msg();
        goto end;
    // diagnostic commands
    } else if (streq(cmd, "status")) {
        bson_t grammars, child;
        char keystr[16];
        const char *key;
        intptr_t language_id = -1;
        if (_engine && draconity->ready) {
            _DSXEngine_GetLanguageID(_engine, &language_id);
        }
        bson_t *doc = BCON_NEW(
            "engine_name", BCON_UTF8(draconity->engine_name.c_str()),
            "success", BCON_BOOL(true),
            "ready", BCON_BOOL(draconity->ready),
            "runtime", BCON_INT64(dr_monotonic_time() - draconity->start_ts),
            "language_id", BCON_INT64(language_id));

        BSON_APPEND_ARRAY_BEGIN(doc, "grammars", &grammars);
        // Iterate over an index and the current grammar
        int i = 0;
        // TODO: Restructure with updated grammar objects
        for (const auto& pair : draconity->grammars) {
            auto grammar = pair.second;
            bson_uint32_to_string(i, &key, keystr, sizeof(keystr));
            BSON_APPEND_DOCUMENT_BEGIN(&grammars, key, &child);
            BSON_APPEND_UTF8(&child, "name", grammar->name.c_str());
            BSON_APPEND_BOOL(&child, "enabled", grammar->enabled);
            BSON_APPEND_INT32(&child, "priority", grammar->priority);
            bson_append_document_end(&grammars, &child);
            i++;
        }
        // dragon psuedo-grammar
        bson_uint32_to_string(draconity->grammars.size(), &key, keystr, sizeof(keystr));
        BSON_APPEND_DOCUMENT_BEGIN(&grammars, key, &child);
        BSON_APPEND_UTF8(&child, "name", "dragon");
        BSON_APPEND_BOOL(&child, "enabled", draconity->dragon_enabled);
        BSON_APPEND_INT32(&child, "priority", 0);
        bson_append_document_end(&grammars, &child);

        bson_append_array_end(doc, &grammars);

        resp = doc;
    } else if (streq(cmd, "mimic")) {
        if (!draconity->ready) goto not_ready;
        if (!phrase_buf || !phrase_len) {
            errmsg = "missing or broken phrase field";
            goto end;
        }
        dsx_dataptr dp = {.data = NULL, .size = 0};
        if (!bson_iter_init_from_data(&iter, phrase_buf, phrase_len)) {
            errmsg = "mimic phrase iter failed";
            goto end;
        }
        // get size of all strings in phrase
        while (bson_iter_next(&iter)) {
            if (!BSON_ITER_HOLDS_UTF8(&iter)) {
                errmsg = "phrase contains non-string value";
                goto end;
            }
            uint32_t length = 0;
            bson_iter_utf8(&iter, &length);
            dp.size += length + 1;
        }
        dp.data = calloc(1, dp.size);
        uint8_t *pos = (uint8_t *)dp.data;
        if (!bson_iter_init_from_data(&iter, phrase_buf, phrase_len)) {
            free(dp.data);
            errmsg = "mimic phrase iter failed";
            goto end;
        }
        int count = 0;
        while (bson_iter_next(&iter)) {
            uint32_t length = 0;
            const char *word = bson_iter_utf8(&iter, &length);
            memcpy(pos, word, length);
            pos += length + 1;
            count++;
        }
        int rc = _DSXEngine_Mimic(_engine, 0, count, &dp, 0, 2);
        free(dp.data);
        if (rc) {
            errstream << "error during mimic: " << rc;
            errmsg = errstream.str();
            goto end;
        }
        // We have to synchronize the mimic callback to a specific mimic.
        // Waiting for the callback will deadlock, so we use a FIFO queue and
        // rely on mimics being queued in the right order. This could be janky.
        draconity->mimic_lock.lock();
        draconity->mimic_queue.push({client_id, tid});
        draconity->mimic_lock.unlock();
        // Response will be sent once mimic completes.
        goto no_response;
    } else {
        goto unsupported_command;
    }
end:
    bson_t *pub;
    free(cmd);
    free(name);

    pub = bson_new();
    BSON_APPEND_BOOL(pub, "success", errmsg.size() == 0);
    if (errmsg.size() > 0 && resp == NULL) {
        resp = BCON_NEW("success", BCON_BOOL(false), "error", BCON_UTF8("request not handled"));
    }
    if (errmsg.size() > 0) {
        bson_free(resp);
        resp = BCON_NEW("success", BCON_BOOL(false), "error", BCON_UTF8(errmsg.c_str()));
    }
    // reinit to reset + appease ASAN
    if (bson_init_static(&root, &msg[0], msg.size())) {
        BSON_APPEND_DOCUMENT(pub, "cmd", &root);
        draconity_publish("cmd", pub);
    }
    return resp;

not_ready:
    errmsg = "engine not ready";
    goto end;

unsupported_command:
    errmsg = "unsupported command";
    goto end;

no_response:
    resp = NULL;
    goto end;
}

void draconity_init() {
    printf("[+] draconity init\n");
    // FIXME: this should just be draconity class init?
    draconity_transport_main(handle_message, draconity->config);
    draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("thread_created")));
    draconity->start_ts = dr_monotonic_time();
}

std::mutex readyLock;

void draconity_ready() {
    readyLock.lock();
    if (!draconity->ready) {
        printf("[+] status: ready\n");
        intptr_t language_id = -1;
        if (_engine) {
            _DSXEngine_GetLanguageID(_engine, &language_id);
        }
        draconity_publish("status",
            BCON_NEW("cmd", BCON_UTF8("ready"),
                     "engine_name", BCON_UTF8(draconity->engine_name.c_str()),
                     "language_id", BCON_INT64(language_id)));
        draconity->ready = true;
    }
    readyLock.unlock();
}

void draconity_attrib_changed(int key, dsx_attrib *attrib) {
    char *attr = attrib->name;
    if (streq(attr, "MICON") && (!draconity->micstate || streq(draconity->micstate, "on"))) {
        draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("mic"), "status", BCON_UTF8("on")));
        draconity->micstate = "on";
    } else if (streq(attr, "MICSTATE")) {
        int64_t micstate = 0;
        _DSXEngine_GetMicState(_engine, &micstate);
        const char *name;
        if (micstate >= 0 && micstate <= 5) {
            name = micstates[micstate];
        } else {
            name = "invalid";
        }
        if (!draconity->micstate || streq(draconity->micstate, name)) {
            draconity->micstate = name;
            draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("mic"), "status", BCON_UTF8(name)));
        }
    } else if (streq(attr, "SPEAKERCHANGED")) {
        draconity_set_default_params();
        // this is slow
        // void *speaker = _DSXEngine_GetCurrentSpeaker(_engine);
        if (!draconity->ready) {
            draconity_ready();
        } else {
            intptr_t language_id = -1;
            if (_engine) {
                _DSXEngine_GetLanguageID(_engine, &language_id);
            }
            draconity_publish("status",
                BCON_NEW("cmd", BCON_UTF8("speaker_change"),
                         "language_id", BCON_INT64(language_id)));
        }
        // state.speaker = speaker;
    } else if (streq(attr, "TOPICCHANGED")) {
        // ?
    }
}

void draconity_mimic_done(int key, dsx_mimic *mimic) {
    draconity->mimic_lock.lock();
    // If we pop too many items, we have a problem, but we still want to protect
    // Draconity a crash.
    if (draconity->mimic_queue.empty()) {
        // TODO: Maybe log this?
        draconity->mimic_lock.unlock();
    } else {
        auto &mimic_info = draconity->mimic_queue.front();
        draconity->mimic_queue.pop();
        draconity->mimic_lock.unlock();
        uint64_t client_id = mimic_info.first;
        uint32_t tid = mimic_info.second;
        draconity_send("mimic",
                       success_msg(),
                       tid,
                       client_id);
    }
}

void draconity_paused(int key, dsx_paused *paused) {
    draconity->handle_pause(paused->token);
}

int draconity_phrase_begin(void *key, void *data) {
    // TODO: atomics? portability?
    return 0;
}

__attribute__((destructor))
static void draconity_shutdown() {
    printf("[-] draconity shutting down\n");
}
