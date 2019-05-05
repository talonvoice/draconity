#include <bson.h>
#include <pthread.h>
#include "draconity.h"
#include "phrase.h"
#include "server.h"

void phrase_to_bson(bson_t *obj, char *phrase) {
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

void result_to_bson(bson_t *obj, dsx_result *result) {
    bson_t words;
    BSON_APPEND_ARRAY_BEGIN(obj, "words", &words);
    char keystr[16];
    const char *key;

    uint32_t paths;
    size_t needed = 0;
    int rc = _DSXResult_BestPathWord(result, 0, &paths, 1, &needed);
    if (rc == 33) {
        uint32_t *paths = calloc(1, needed);
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
        free(paths);
    }
    bson_append_array_end(obj, &words);
}

static char *safe_gkey_to_name(void *key) {
    char *name = NULL;
    pthread_mutex_lock(&draconity_state.keylock);
    draconity_grammar *g = tack_get(&draconity_state.gkeys, (int)key);
    if (g) name = strdup(g->name);
    pthread_mutex_unlock(&draconity_state.keylock);
    return name;
}

int phrase_publish(void *key, dsx_end_phrase *endphrase, const char *cmd, bool hypothesis) {
    char *name = safe_gkey_to_name(key);
    if (!name) goto end;

    bool accept = (endphrase->flags & 1) == 1;
    bool ours = (endphrase->flags & 2) == 2;
    bson_t obj = BSON_INITIALIZER;
    BSON_APPEND_UTF8(&obj, "cmd", cmd);
    BSON_APPEND_UTF8(&obj, "grammar", name);
    free(name);
    if ((accept && ours) || hypothesis) {
        phrase_to_bson(&obj, endphrase->phrase);
        result_to_bson(&obj, endphrase->result);
        // TODO: figure out when all I should call destroy
    } else {
        bson_t array;
        BSON_APPEND_ARRAY_BEGIN(&obj, "phrase", &array);
        bson_append_array_end(&obj, &array);
    }
    draconity_publish("phrase", &obj);
end:
    _DSXResult_Destroy(endphrase->result);
    return 0;
}

int phrase_end(void *key, dsx_end_phrase *endphrase) {
    return phrase_publish(key, endphrase, "p.end", false);
}

int phrase_hypothesis(void *key, dsx_end_phrase *endphrase) {
    return phrase_publish(key, endphrase, "p.hypothesis", true);
}

int phrase_begin(void *key, void *data) {
    char *name = safe_gkey_to_name(key);
    if (!name) return 0;
    draconity_publish("phrase", BCON_NEW("cmd", BCON_UTF8("p.begin"), "grammar", BCON_UTF8(name)));
    free(name);
    return 0;
}
