#include <sstream>

#include <bson.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "transport.h"
#include "phrase.h"
#include "server.h"
#include "draconity.h"

#define align4(len) ((len + 4) & ~3)

#ifndef streq
#define streq(a, b) !strcmp(a, b)
#endif

#define US  1
#define MS  (1000 * US)
#define SEC (1000 * MS)

void draconity_publish(const char *topic, bson_t *obj) {
    BSON_APPEND_INT64(obj, "ts", bson_get_monotonic_time());
    uint32_t length = 0;
    uint8_t *buf = bson_destroy_with_steal(obj, true, &length);
    draconity_transport_publish(topic, buf, length);
}

void draconity_logf(const char *fmt, ...) {
    char *str = NULL;
    va_list va;
    va_start(va, fmt);
    vasprintf(&str, fmt, va);
    va_end(va);

    bson_t obj = BSON_INITIALIZER;
    BSON_APPEND_UTF8(&obj, "msg", str);
    draconity_publish("log", &obj);
    free(str);
}

static bson_t *success_msg() {
    return BCON_NEW("success", BCON_BOOL(true));
}

static int grammar_disable(Grammar *grammar, char **errmsg) {
    int rc = 0;
    if ((rc =_DSXGrammar_Deactivate(grammar->handle, 0, grammar->main_rule))) {
        asprintf(errmsg, "error deactivating grammar: %d", rc);
        return rc;
    }
    grammar->enabled = false;
    if ((rc =_DSXGrammar_Unregister(grammar->handle, grammar->endkey))) {
        asprintf(errmsg, "error removing end cb: %d", rc);
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(grammar->handle, grammar->hypokey))) {
        asprintf(errmsg, "error removing hypothesis cb: %d", rc);
        return rc;
    }
    if ((rc = _DSXGrammar_Unregister(grammar->handle, grammar->beginkey))) {
        asprintf(errmsg, "error removing begin cb: %d", rc);
        return rc;
    }
    return 0;
}

static void grammar_free(Grammar *g) {
    free((void *)g->main_rule);
    free((void *)g->name);
    free((void *)g->appname);
    memset(g, 0, sizeof(Grammar));
    free(g);
}

static int grammar_unload(Grammar *g) {
    char *disablemsg = NULL;
    int rc = 0;
    if (g->enabled) {
        if ((rc = grammar_disable(g, &disablemsg))) {
            draconity_logf("during unload: %s", disablemsg);
            free(disablemsg);
            return rc;
        }
    }
    rc = _DSXGrammar_Destroy(g->handle);

    draconity->keylock.lock();
    draconity->grammars.erase(g->name);
    // Don't erase the key outright yet - we make it null for now to avoid race
    // conditions. See comment in `g.load` for full explanation.
    draconity->gkeys[g->key] = NULL;

    reusekey *reuse = new reusekey;
    reuse->key = g->key;
    reuse->ts = bson_get_monotonic_time();
    reuse->serial = draconity->serial;
    draconity->gkfree.push_back(reuse);

    grammar_free(g);
    draconity->keylock.unlock();
    return rc;
}

static bson_t *handle_message(const uint8_t *msg, uint32_t msglen) {
    std::ostringstream errstream;
    std::string errmsg = "";

    Grammar *grammar = NULL;
    char *cmd = NULL, *name = NULL, *list = NULL, *main_rule = NULL;
    bool enabled = false, exclusive = false;
    int priority = 0, counter;
    bool has_enabled = false, has_exclusive = false, has_priority = false;

    const uint8_t *items_buf = NULL, *data_buf = NULL, *phrase_buf = NULL, *words_buf;
    uint32_t items_len = 0, data_len = 0, phrase_len = 0, words_len;

    bson_t *resp = NULL;
    bson_t root;
    if (!bson_init_static(&root, msg, msglen)) {
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
            } else if (streq(key, "main_rule") && BSON_ITER_HOLDS_UTF8(&iter)) {
                main_rule = bson_iter_dup_utf8(&iter, NULL);
            } else if (streq(key, "list") && BSON_ITER_HOLDS_UTF8(&iter)) {
                list = bson_iter_dup_utf8(&iter, NULL);
            } else if (streq(key, "enabled") && BSON_ITER_HOLDS_BOOL(&iter)) {
                enabled = bson_iter_bool(&iter);
                has_enabled = true;
            } else if (streq(key, "exclusive") && BSON_ITER_HOLDS_BOOL(&iter)) {
                exclusive = bson_iter_bool(&iter);
                has_exclusive = true;
            } else if (streq(key, "priority") && BSON_ITER_HOLDS_INT32(&iter)) {
                priority = bson_iter_int32(&iter);
                has_priority = true;
            } else if (streq(key, "items") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &items_len, &items_buf);
            } else if (streq(key, "phrase") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &phrase_len, &phrase_buf);
            } else if (streq(key, "words") && BSON_ITER_HOLDS_ARRAY(&iter)) {
                bson_iter_array(&iter, &words_len, &words_buf);
            } else if (streq(key, "data") && BSON_ITER_HOLDS_BINARY(&iter)) {
                bson_iter_binary(&iter, NULL, &data_len, &data_buf);
            } else if (streq(key, "pingpong-counter") && BSON_ITER_HOLDS_INT32(&iter)) {
                counter = bson_iter_int32(&iter);
            }
        }
    }
    if (name)
        grammar = draconity->grammar_get(name);
    if (!cmd) {
        errmsg = "missing or broken cmd field";
        goto end;
    }
    if (streq(cmd, "ping")) {
        printf("[+] Draconity ping/pong: recognized ping! Received counter is %" PRIi32
               "; responding with %" PRIi32 "\n", counter, counter + 1);
        counter++;
        bson_t *response = bson_new();
        BSON_APPEND_UTF8(response, "cmd", "pong");
        BSON_APPEND_INT32(response, "pingpong-counter", counter);
        resp = response;
#if RUN_IN_DRAGON
    } else if (cmd[0] == 'w') {
        if (!draconity->ready) goto not_ready;
        if (!_DSXEngine_EnumWords ||
                !_DSXWordEnum_Next ||
                !_DSXEngine_AddWord ||
                !_DSXEngine_DeleteWord ||
                !_DSXEngine_ValidateWord) {
            errmsg = "engine does not support vocabulary editing";
            goto end;
        }
        bson_t *response = bson_new();
        char keystr[16];
        const char *index;
        int i = 0;
        bool all_good = true;
        bson_t words;
        BSON_APPEND_ARRAY_BEGIN(response, "words", &words);
        if (streq(cmd, "w.list")) {
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
        } else {
            if (!words_buf || !words_len) {
                bson_free(response);
                errmsg = "missing or broken words field";
                goto end;
            }
            if (!bson_iter_init_from_data(&iter, words_buf, words_len)) {
                bson_free(response);
                errmsg = "word iter failed";
                goto end;
            }
            while (bson_iter_next(&iter)) {
                if (!BSON_ITER_HOLDS_UTF8(&iter)) {
                    bson_free(response);
                    errmsg = "words contains non-string value";
                    goto end;
                }
                const char *word = bson_iter_utf8(&iter, NULL);
                int result = 0;
                if (streq(cmd, "w.add")) {
                    drg_wordinfo info = {.flags=1, .num=1, .flags2=0x4000 /* temporary */, .flags3=0, .tag=0};
                    drg_wordinfo *infoptr = &info;
                    result = _DSXEngine_AddTemporaryWord(_engine, word, 1);
                } else if (streq(cmd, "w.remove")) {
                    result = _DSXEngine_DeleteWord(_engine, 1, word);
                } else if (streq(cmd, "w.test")) {
                    bool valid = false;
                    result = _DSXEngine_ValidateWord(_engine, word, &valid);
                    result = !(result == 0 && valid);
                } else {
                    bson_free(response);
                    goto unsupported_command;
                }
                if (result != 0) {
                    all_good = false;
                    bson_uint32_to_string(i++, &index, keystr, sizeof(keystr));
                    BSON_APPEND_UTF8(&words, index, word);
                }
            }
        }
        bson_append_array_end(response, &words);
        BSON_APPEND_BOOL(response, "success", all_good);
        resp = response;
    } else if (streq(cmd, "ready")) {
        draconity_ready();
        resp = success_msg();
    } else if (cmd[0] == 'g') {
        if (streq(cmd, "g.update")) {
            if (!draconity->ready) goto not_ready;
            if (streq(name, "dragon")) {
                draconity->set_dragon_enabled(enabled);
                /*
                draconity->dragon_lock.lock();
                if (has_enabled && enabled != state.dragon_enabled) {
                    tack_foreach(&state.dragon_grammars, fg) {
                        int rc;
                        if (enabled) {
                            if ((rc = _DSXGrammar_Activate(fg->grammar, fg->unk1, fg->unk2, fg->main_rule))) {
                                errstream << "error activating grammar: " << rc;
                                errmsg = errstream.str();
                                break;
                            }
                        } else {
                            if ((rc =_DSXGrammar_Deactivate(grammar->handle, fg->unk1, grammar->main_rule))) {
                                errstream << "error deactivating grammar: " << rc;
                                errmsg = errstream.str();
                                break;
                            }
                        }
                    }
                    state.dragon_enabled = enabled;
                }
                draconity->dragon_lock.unlock();
                */
                if (errmsg.size() == 0) resp = success_msg();
                goto end;
            }
            if (!grammar) goto no_grammar;

            if (has_enabled && enabled != grammar->enabled) {
                if (enabled) {
                    int rc = 0;
                    if ((rc = _DSXGrammar_Activate(grammar->handle, 0, false, grammar->main_rule))) {
                        errstream << "error activating grammar: " << rc;
                        errmsg = errstream.str();
                        goto end;
                    }
                    if ((rc = _DSXGrammar_RegisterEndPhraseCallback(grammar->handle, phrase_end, (void *)grammar->key, &grammar->endkey))) {
                        errstream << "error registering end phrase callback: " << rc;
                        errmsg = errstream.str();
                        goto end;
                    }
                    if ((rc = _DSXGrammar_RegisterPhraseHypothesisCallback(grammar->handle, phrase_hypothesis, (void *)grammar->key, &grammar->hypokey))) {
                        errstream << "error registering phrase hypothesis callback: " << rc;
                        errmsg = errstream.str();
                        goto end;
                    }
                    if ((rc = _DSXGrammar_RegisterBeginPhraseCallback(grammar->handle, phrase_begin, (void *)grammar->key, &grammar->beginkey))) {
                        errstream << "error registering begin phrase callback: " << rc;
                        errmsg = errstream.str();
                        goto end;
                    }
                } else {
                    errmsg = draconity->grammar_disable(grammar);
                    if (errmsg.size() > 0) {
                        goto end;
                    }
                }
                grammar->enabled = enabled;
            }
            if (has_exclusive && exclusive != grammar->exclusive) {
                int rc = _DSXGrammar_SetSpecialGrammar(grammar->handle, exclusive);
                if (rc) {
                    errstream << "error setting exclusive grammar: " << rc;
                    errmsg = errstream.str();
                    goto end;
                }
                grammar->exclusive = exclusive;
            }
            if (has_priority && priority != grammar->priority) {
                int rc = _DSXGrammar_SetPriority(grammar->handle, priority);
                if (rc) {
                    errstream << "error setting priority: " << rc;
                    errmsg = errstream.str();
                    goto end;
                }
                grammar->priority = priority;
            }
            resp = success_msg();
        } else if (streq(cmd, "g.listset")) {
            if (!draconity->ready) goto not_ready;
            if (!grammar) goto no_grammar;
            if (!list) {
                errmsg = "missing or broken list name";
                goto end;
            }
            if (!items_buf || !items_len) {
                errmsg = "missing or broken items array";
                goto end;
            }
            dsx_dataptr dp = {.data = NULL, .size = 0};
            if (!bson_iter_init_from_data(&iter, items_buf, items_len)) {
                errmsg = "list item iter failed";
                goto end;
            }
            // get size of the new list's data block
            while (bson_iter_next(&iter)) {
                if (!BSON_ITER_HOLDS_UTF8(&iter)) {
                    errmsg = "list contains non-string value";
                    goto end;
                }
                uint32_t length = 0;
                bson_iter_utf8(&iter, &length);
                dp.size += sizeof(dsx_id) + align4(length);
            }
            dp.data = calloc(1, dp.size);
            uint8_t *pos = (uint8_t *)dp.data;
            if (!bson_iter_init_from_data(&iter, items_buf, items_len)) {
                errmsg = "list item iter failed";
                goto end;
            }
            while (bson_iter_next(&iter)) {
                dsx_id *ent = (dsx_id *)pos;
                uint32_t length = 0;
                const char *word = bson_iter_utf8(&iter, &length);
                ent->size = sizeof(dsx_id) + align4(length);
                memcpy(ent->name, word, length);
                pos += ent->size;
            }
            draconity->dragon_lock.lock();
            int ret = _DSXGrammar_SetList(grammar->handle, list, &dp);
            draconity->dragon_lock.unlock();
            if (ret) {
                errmsg = "error setting list";
                goto end;
            }
            resp = success_msg();
            free(dp.data);
        } else if (streq(cmd, "g.unload")) {
            if (!draconity->ready) goto not_ready;
            if (!grammar) goto no_grammar;
            int rc = draconity->grammar_unload(grammar);
            if (rc) {
                errstream << "error unloading grammar: " << rc;
                errmsg = errstream.str();
                goto end;
            }
            resp = success_msg();
        } else if (streq(cmd, "g.load")) {
            if (!draconity->ready) goto not_ready;
            if (streq(name, "dragon")) {
                errmsg = "'dragon' grammar name is reserved";
                goto end;
            }
            if (!data_buf || !data_len) {
                errmsg = "missing or broken data field";
                goto end;
            }
            if (!main_rule) {
                errmsg = "missing main_rule";
                goto end;
            }
            if (grammar) {
                draconity_logf("warning: reloading \"%s\"", name);
                errmsg = draconity->grammar_unload(grammar);
                if (errmsg.size() > 0)
                    goto end;
            }
            grammar = new Grammar(name, main_rule);
            // FIXME: this still needs to be cleaned up as part of porting to C++
            draconity->grammar_load(grammar);
            dsx_dataptr dp = {.data = (void *)data_buf, .size = data_len};

            int ret = _DSXEngine_LoadGrammar(_engine, 1 /*cfg*/, &dp, &grammar->handle);
            if (ret > 0) {
                errstream << "error loading grammar: " << ret;
                errmsg = errstream.str();
                delete grammar;
                goto end;
            }
            draconity->grammars[grammar->name] = grammar;
            // keys are used to associate grammar objects with callbacks
            // in a way that allows us to free the grammar objects without crashing if a callback was in flight
            // once freed, keys can be reused after 30 seconds, or if the global serial has increased by at least 3
            // (to prevent both callback confusion and key explosion)
            bool reused = false;
            int64_t now = bson_get_monotonic_time();
            pthread_mutex_lock(draconity->keylock);
            for (reusekey * reuse : draconity->gkfree) {
                if (now - reuse->ts > 30 * SEC || reuse->serial + 3 <= draconity->serial) {
                    reused = true;
                    grammar->key = reuse->key;
                    free(reuse);
                    draconity->gkfree.erase(i);
                    draconity->gkeys[grammar->key] = grammar;
                    break;
                }
            }
            if (!reused) {
                grammar->key = draconity->gkeys.size();
                draconity->gkeys.push_back(grammar);
            }
            pthread_mutex_unlock(draconity->keylock);
            // printf("%d\n", _DSXGrammar_SetApplicationName(grammar->handle, grammar->name));
            resp = success_msg();
        } else {
            goto unsupported_command;
        }
    // diagnostic commands
    } else if (streq(cmd, "status")) {
        bson_t grammars, child;
        char keystr[16];
        const char *key;
        bson_t *doc = BCON_NEW(
            "success", BCON_BOOL(true),
            "ready", BCON_BOOL(draconity->ready),
            "runtime", BCON_INT64(bson_get_monotonic_time() - draconity->start_ts));

        BSON_APPEND_ARRAY_BEGIN(doc, "grammars", &grammars);
        for (Grammar * grammar : draconity->grammars) {
            bson_uint32_to_string(i, &key, keystr, sizeof(keystr));
            BSON_APPEND_DOCUMENT_BEGIN(&grammars, key, &child);
            BSON_APPEND_UTF8(&child, "name", grammar->name);
            BSON_APPEND_BOOL(&child, "enabled", grammar->enabled);
            BSON_APPEND_INT32(&child, "priority", grammar->priority);
            BSON_APPEND_BOOL(&child, "exclusive", grammar->exclusive);
            bson_append_document_end(&grammars, &child);
        }
        // dragon psuedo-grammar
        bson_uint32_to_string(draconity->grammars.size(), &key, keystr, sizeof(keystr));
        BSON_APPEND_DOCUMENT_BEGIN(&grammars, key, &child);
        BSON_APPEND_UTF8(&child, "name", "dragon");
        BSON_APPEND_BOOL(&child, "enabled", draconity->dragon_enabled);
        BSON_APPEND_INT32(&child, "priority", 0);
        BSON_APPEND_BOOL(&child, "exclusive", false);
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
        draconity->mimic_lock.lock();
        draconity->mimic_success = false;

        int rc = _DSXEngine_Mimic(_engine, 0, count, &dp, 0, 2);
        if (rc) {
            errstream << "error during mimic: " << rc;
            free(dp.data);
            draconity->mimic_lock.unlock();
            goto end;
        }
        free(dp.data);
        // TODO: if dragon fails to issue callback, draconity will hang forever
        struct timespec timeout = {.tv_sec = 10, .tv_nsec = 0};
        // FIXME: port timedwait to C++
        // rc = pthread_cond_timedwait_relative_np(&state.mimic_cond, &state.mimic_lock, &timeout);
        draconity->mimic_lock.unlock();
        bool success = (rc == 0 && draconity->mimic_success);
        if (!success) {
            errmsg = "mimic failed";
            goto end;
        }
        resp = success_msg();
#endif //RUN_IN_DRAGON
    } else {
        goto unsupported_command;
    }
    bson_t *pub;
end:
    free(cmd);
    free(name);
    free(main_rule);
    free(list);

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
    if (bson_init_static(&root, msg, msglen)) {
        BSON_APPEND_DOCUMENT(pub, "cmd", &root);
        draconity_publish("cmd", pub);
    }
    return resp;

no_grammar:
    errmsg = "grammar not found";
    goto end;

not_ready:
    errmsg = "engine not ready";
    goto end;

unsupported_command:
    errmsg = "unsupported command";
    goto end;
}

void draconity_init() {
    printf("[+] draconity init\n");
    // FIXME: this should just be draconity class init?
    draconity_transport_main(handle_message);
    draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("thread_created")));
    draconity->start_ts = bson_get_monotonic_time();
}

static const char *micstates[] = {
    "disabled",
    "off",
    "on",
    "sleeping",
    "pause",
    "resume",
};

void draconity_ready() {
    if (!draconity->ready) {
        printf("[+] status: ready\n");
        draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("ready")));
        draconity->ready = true;
    }
}

void draconity_attrib_changed(int key, dsx_attrib *attrib) {
    char *attr = attrib->name;
    if (streq(attr, "MICON") && (!draconity->micstate || streq(draconity->micstate, "on"))) {
        draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("mic"), "status", BCON_UTF8("on")));
        draconity->micstate = "on";
    } else if (streq(attr, "MICSTATE")) {
        int64_t micstate = 0;
#if RUN_IN_DRAGON
        _DSXEngine_GetMicState(_engine, &micstate);
        const char *name;
        if (micstate >= 0 && micstate <= 5) {
            name = micstates[micstate];
        } else {
            name = "invalid";
        }
#else //RUN_IN_DRAGON
        const char *name = "nodragon";
#endif
        if (!draconity->micstate || streq(draconity->micstate, name)) {
            draconity->micstate = name;
            draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("mic"), "status", BCON_UTF8(name)));
        }
    } else if (streq(attr, "SPEAKERCHANGED")) {
        draconity_set_default_params();
        // this is slow
        // void *speaker = _DSXEngine_GetCurrentSpeaker(_engine);
        draconity_ready();
        // state.speaker = speaker;
    } else if (streq(attr, "TOPICCHANGED")) {
        // ?
    }
}

void draconity_mimic_done(int key, dsx_mimic *mimic) {
    draconity->mimic_lock.lock();
    draconity->mimic_success = true;
    // FIXME: signal the condvar
    // pthread_cond_signal(&state.mimic_cond);
    draconity->mimic_lock.unlock();
}

void draconity_paused(int key, dsx_paused *paused) {
#if RUN_IN_DRAGON
    _DSXEngine_Resume(_engine, paused->token);
#else
    printf("draconity not running in dragon so cannot call _DSXEngine_Resume() from draconity_paused()\n");
#endif
}

int draconity_phrase_begin(void *key, void *data) {
    // TODO: atomics? portability?
    draconity->keylock.lock();
    draconity->serial++;
    draconity->keylock.unlock();
    return 0;
}

__attribute__((destructor))
static void draconity_shutdown() {
    printf("[-] draconity shutting down\n");
}
