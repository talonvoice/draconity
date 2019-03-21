#ifndef TACK_H
#define TACK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct tack_t {
    void **data;
    struct tack_t *hash;
    int len, cap, pos;
} tack_t;

extern char *tack_str_join(tack_t *tack, const char *sep);
extern int tack_len(tack_t *tack);
extern void **tack_raw(tack_t *tack);
extern void *tack_cur(tack_t *tack);
extern void *tack_first(tack_t *tack);
extern void *tack_get(tack_t *tack, int idx);
extern void *tack_peek(tack_t *tack);
extern void *tack_pop(tack_t *tack);
extern void *tack_shift(tack_t *tack);
extern void tack_clear(tack_t *tack);
extern void tack_del(tack_t *tack, int idx);
extern void tack_push(tack_t *tack, void *data);
extern void tack_remove(tack_t *tack, void *data);
extern void tack_set(tack_t *tack, int idx, void *data);

extern uintptr_t tack_get_int(tack_t *tack, int idx);
extern uintptr_t tack_peek_int(tack_t *tack);
extern uintptr_t tack_pop_int(tack_t *tack);
extern void tack_push_int(tack_t *tack, uintptr_t val);
extern void tack_set_int(tack_t *tack, int idx, uintptr_t val);

extern bool tack_hexists(tack_t *tack, const char *key);
extern void *tack_hget(tack_t *tack, const char *key);
extern void *tack_hset(tack_t *tack, const char *key, void *val);
extern void tack_hdel(tack_t *tack, const char *key);

#define tack_foreach(tack, name) for (int i = 0; (name = tack_get(tack, i)); i++)

#endif
