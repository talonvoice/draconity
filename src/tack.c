#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tack.h"

#ifndef MAX
# define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define TACK_DEFAULT_SIZE 8
// must be a power of two
#define TACK_HASH_SIZE 2048

static bool tack_pop_bad(tack_t *tack)   { return (tack == NULL || tack->len <= 0); }
static bool tack_shift_bad(tack_t *tack) { return (tack == NULL || tack->pos >= tack->len); }

static bool tack_grow(tack_t *tack, int idx) {
    if (tack->data == NULL) {
        tack->cap = TACK_DEFAULT_SIZE;
        tack->data = malloc(sizeof(void *) * tack->cap);
        if (tack->data != NULL) {
            return true;
        }
    } else if (MAX(tack->len, idx) >= tack->cap) {
        if (tack->cap > INT_MAX >> 1) {
            tack->cap = INT_MAX;
            if (tack->len == INT_MAX) {
                return false;
            }
        } else {
            tack->cap = MAX(tack->cap * 2, tack->len + idx);
        }
        void **new = realloc(tack->data, sizeof(void *) * tack->cap);
        if (new != NULL) {
            tack->data = new;
            return true;
        }
    } else {
        return true;
    }
    fprintf(stderr, "warning: tack_grow() to %d failed\n", tack->cap);
    return false;
}

void tack_del(tack_t *tack, int idx) {
    if (idx < 0 || idx >= tack->len)
        return;

    tack->len -= 1;
    memmove(&tack->data[idx], &tack->data[idx+1], (tack->len - idx) * sizeof(void *));
    if (idx <= tack->pos && tack->pos > 0) {
        tack->pos -= 1;
    }
}

void tack_remove(tack_t *tack, void *data) {
    void *item;
    tack_foreach(tack, item) {
        if (item == data) {
            tack_del(tack, i);
            break;
        }
    }
}

void tack_clear(tack_t *tack) {
    free(tack->data);
    tack->data = NULL;
    if (tack->hash) {
        for (int i = 0; i < TACK_HASH_SIZE; i++) {
            tack_clear(&tack->hash[i]);
        }
        free(tack->hash);
    }
    tack->hash = NULL;
    tack->pos = 0;
    tack->cap = 0;
    tack->len = 0;
}

int tack_len(tack_t *tack) { return tack->len; }
void **tack_raw(tack_t *tack) { return tack->data; }

void tack_push(tack_t *tack, void *data) {
    if (tack_grow(tack, 0))
        tack->data[tack->len++] = data;
}

void *tack_pop(tack_t *tack) {
    if (tack_pop_bad(tack))
        return NULL;
    return tack->data[--tack->len];
}

void *tack_peek(tack_t *tack) {
    if (tack_pop_bad(tack))
        return NULL;
    return tack->data[tack->len - 1];
}

void *tack_get(tack_t *tack, int idx) {
    if (tack == NULL || idx < 0 || idx >= tack->len)
        return NULL;
    return tack->data[idx];
}

void tack_set(tack_t *tack, int idx, void *data) {
    if (tack_grow(tack, idx)) {
        int len = MAX(tack->len, idx + 1);
        for (int i = tack->len; i < len; i++) {
            tack->data[i] = NULL;
        }
        tack->data[idx] = data;
        tack->len = MAX(tack->len, idx + 1);
    }
}

void *tack_cur(tack_t *tack) {
    if (tack_shift_bad(tack))
        return NULL;
    return tack->data[tack->pos];
}

void *tack_shift(tack_t *tack) {
    if (tack_shift_bad(tack))
        return NULL;
    return tack->data[tack->pos++];
}

char *tack_str_join(tack_t *tack, const char *sep) {
    if (tack->len == 0)
        return NULL;

    size_t sep_len = strlen(sep);
    size_t len = 0;
    char * const*array = (char **)tack->data;
    // a length-encoded string library would be really nice here
    for (int i = 0; i < tack->len; i++) {
        if (array[i] != NULL) {
            len += strlen(array[i]);
        }
    }
    len += sep_len * (tack->len - 1);
    char *out = malloc(len * sizeof(char) + 1);
    out[len] = '\0';
    char *pos = out;
    for (int i = 0; i < tack->len; i++) {
        if (array[i] != NULL) {
            int slen = strlen(array[i]);
            memcpy(pos, array[i], slen);
            pos += slen;
            if (i < tack->len - 1) {
                memcpy(pos, sep, sep_len);
                pos += sep_len;
            }
        }
    }
    return out;
}

uintptr_t tack_get_int(tack_t *tack, int idx)           { return (uintptr_t)tack_get(tack, idx); }
uintptr_t tack_peek_int(tack_t *tack)                   { return (uintptr_t)tack_peek(tack); }
uintptr_t tack_pop_int(tack_t *tack)                    { return (uintptr_t)tack_pop(tack); }
void tack_push_int(tack_t *tack, uintptr_t val)         { tack_push(tack, (void *)val); }
void tack_set_int(tack_t *tack, int idx, uintptr_t val) { tack_set(tack, idx, (void *)val); }

/* hash table implementation */

uint32_t djb_hash(const char *key) {
    uint32_t h = 0;
    while (*key) h = 33 * h ^ ((uint8_t)*key++);
    return h & (TACK_HASH_SIZE-1);
}

// TODO: make this a linked list rather than a tack_t
typedef struct {
    const char *key;
    void *data;
} tack_hash_entry;

void *tack_hset(tack_t *tack, const char *key, void *val) {
    if (tack->hash == NULL)
        tack->hash = calloc(1, sizeof(tack_t) * TACK_HASH_SIZE);

    tack_t *slot = &tack->hash[djb_hash(key)];
    tack_hash_entry *entry;
    // collision - make sure we're not already in this slot
    tack_foreach(slot, entry) {
        // colliding slot found
        if (strcmp(entry->key, key) == 0) {
            // return the old data so it can be caller-freed
            void *tmp = entry->data;
            entry->data = val;
            return tmp;
        }
    }
    // no collision - need to push a new entry
    entry = malloc(sizeof(tack_hash_entry));
    entry->key = strdup(key);
    entry->data = val;
    tack_push(slot, entry);
    return NULL;
}

void *tack_hget(tack_t *tack, const char *key) {
    if (! tack->hash)
        return NULL;

    tack_t *slot = &tack->hash[djb_hash(key)];
    tack_hash_entry *entry;
    tack_foreach(slot, entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->data;
        }
    }
    return NULL;
}

bool tack_hexists(tack_t *tack, const char *key) {
    if (!tack || !tack->hash || !key)
        return NULL;

    tack_t *slot = &tack->hash[djb_hash(key)];
    tack_hash_entry *entry;
    tack_foreach(slot, entry) {
        if (strcmp(entry->key, key) == 0) {
            return true;
        }
    }
    return false;
}

void tack_hdel(tack_t *tack, const char *key) {
    if (! tack->hash)
        return;

    tack_t *slot = &tack->hash[djb_hash(key)];
    tack_hash_entry *entry;
    tack_foreach(slot, entry) {
        if (strcmp(entry->key, key) == 0) {
            free(entry);
            if (tack_len(slot) == 0) {
                tack_clear(slot);
            } else {
                tack_del(slot, i);
            }
            break;
        }
    }
}
