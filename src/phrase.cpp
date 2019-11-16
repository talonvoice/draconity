#include <bson.h>
#include "draconity.h"
#include "phrase.h"
#include "server.h"
#include "transport/transport.h"

extern "C" {

static void phrase_to_bson(bson_t *obj, char *phrase) {
    bson_t array;
    char keystr[16];
    const char *key;

    BSON_APPEND_ARRAY_BEGIN(obj, "phrase", &array);
    uint32_t len = *(uint32_t *)phrase;
    char *end = phrase + len;
    char *pos = phrase + 4;
    int i = 0;
    while (pos < end) {
        dsx_id *ent = (dsx_id *)pos;
        bson_uint32_to_string(i++, &key, keystr, sizeof(keystr));
        BSON_APPEND_UTF8(&array, key, ent->name);
        pos += ent->size;
    }
    bson_append_array_end(obj, &array);
}

static void result_to_bson(bson_t *obj, dsx_result *result) {
    bson_t words;
    BSON_APPEND_ARRAY_BEGIN(obj, "words", &words);
    char keystr[16];
    const char *key;

    uint32_t paths;
    size_t needed = 0;
    int rc = _DSXResult_BestPathWord(result, 0, &paths, 1, &needed);
    if (rc == 33) {
        uint32_t *paths = new uint32_t[needed];
        rc = _DSXResult_BestPathWord(result, 0, paths, needed, &needed);
        if (rc == 0) {
            dsx_word_node node;
            // get the rule number and cfg node information for each word
            for (uint32_t i = 0; i < needed / sizeof(uint32_t); i++) {
                uint32_t id = 0;
                char *word = NULL;
                rc = _DSXResult_GetWordNode(result, paths[i], &node, &id, &word);
                if (rc || word == NULL) {
                    break;
                }
                bson_t wdoc;
                bson_uint32_to_string(i, &key, keystr, sizeof(keystr));
                BSON_APPEND_DOCUMENT_BEGIN(&words, key, &wdoc);
                BSON_APPEND_UTF8(&wdoc, "word", word);
                BSON_APPEND_INT32(&wdoc, "id", id);
                BSON_APPEND_INT32(&wdoc, "rule", node.rule);
                BSON_APPEND_INT64(&wdoc, "start", node.start_time);
                BSON_APPEND_INT64(&wdoc, "end", node.end_time);
                bson_append_document_end(&words, &wdoc);
            }
        }
        delete []paths;
    }
    bson_append_array_end(obj, &words);
}

void phrase_publish(void *key, char *phrase, dsx_result *result, const char *cmd, bool use_result) {
    bson_t obj = BSON_INITIALIZER;
    std::shared_ptr<Grammar> grammar = draconity->get_grammar((uintptr_t)key);
    if (grammar == NULL) return;

    BSON_APPEND_UTF8(&obj, "cmd", cmd);
    BSON_APPEND_UTF8(&obj, "grammar", grammar->name.c_str());
    if (use_result) {
        phrase_to_bson(&obj, phrase);
        result_to_bson(&obj, result);
    } else {
        bson_t array;
        BSON_APPEND_ARRAY_BEGIN(&obj, "phrase", &array);
        bson_append_array_end(&obj, &array);
    }
    draconity_publish_one("phrase", &obj, PUBLISH_TID, grammar->state.client_id);
}

int phrase_end(void *key, dsx_end_phrase *endphrase) {
    bool accept = (endphrase->flags & 1) == 1;
    bool ours = (endphrase->flags & 2) == 2;

    phrase_publish(key, endphrase->phrase, endphrase->result, "p.end", (accept && ours));
    _DSXResult_Destroy(endphrase->result);
    return 0;
}

int phrase_hypothesis(void *key, dsx_hypothesis *hypothesis) {
    phrase_publish(key, hypothesis->phrase, hypothesis->result, "p.hypothesis", true);
    _DSXResult_Destroy(hypothesis->result);
    return 0;
}

int phrase_begin(void *key, void *data) {
    std::shared_ptr<Grammar> grammar = draconity->get_grammar((uintptr_t)key);
    if (grammar == NULL) return 0;
    draconity_publish_one("phrase",
                          BCON_NEW("cmd", BCON_UTF8("p.begin"),
                                   "grammar", BCON_UTF8(grammar->name.c_str())),
                          PUBLISH_TID,
                          grammar->state.client_id);
    return 0;
}

} // extern "C"
