#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "draconity.h"
#include "abstract_platform.h"

#include "api.h"

int draconity_set_param(const char *key, const char *value) {
    if (!_engine) return -1;
    void *param = _DSXEngine_GetParam(_engine, key);
    int ret = _DSXEngine_SetStringValue(_engine, param, value);
    _DSXEngine_DestroyParam(_engine, param);
    return ret;
}

void draconity_set_default_params() {
    auto timeout = std::to_string(draconity->timeout);
    auto timeout_incomplete = std::to_string(draconity->timeout);
    draconity_set_param("DwTimeOutComplete", timeout.c_str());
    draconity_set_param("DwTimeOutIncomplete", timeout_incomplete.c_str());

    draconity_set_param("DemonThreadPhraseFinishWait", "0");
    // draconity_set_param("TwoPassSkipSecondPass", "1");
    // draconity_set_param("ReturnPhonemes", "1");
    // draconity_set_param("ReturnNoise", "1");
    // draconity_set_param("ReturnPauseFillers", "1");
    // draconity_set_param("Pass2DefaultSpeed", "1");
    draconity_set_param("NumWordsAvailable", "10000");
    // draconity_set_param("MinPartialUpdateCFGTime", "1");
    // draconity_set_param("MinPartialUpdateTime", "1");
    // draconity_set_param("LiveMicMinStopFrames", "1");
    // draconity_set_param("SkipLettersVocInVocLookup", "1");
    draconity_set_param("UseParallelRecognizers", "1");
    // draconity_set_param("UsePitchTracking", "1");
    draconity_set_param("Pass1A_DurationThresh_ms", "50");
    // draconity_set_param("ComputeSpeed", "10");
    // draconity_set_param("DisableWatchdog", "1");
    // draconity_set_param("DoBWPlus", "1");
    draconity_set_param("ExtraDictationWords", "10000");
    draconity_set_param("MaxCFGWords", "20000");
    draconity_set_param("MaxPronGuessedWords", "20000");
    draconity_set_param("PhraseHypothesisCallbackThread", "1");
}

static void engine_setup(drg_engine *engine) {
    static unsigned int cb_key = 0;
    int ret = _DSXEngine_RegisterAttribChangedCallback(engine, draconity_attrib_changed, NULL, &cb_key);
    if (ret) draconity_logf("error adding attribute callback: %d", ret);

    ret = _DSXEngine_RegisterMimicDoneCallback(engine, draconity_mimic_done, NULL, &cb_key);
    if (ret) draconity_logf("error adding mimic done callback: %d", ret);

    ret = _DSXEngine_RegisterPausedCallback(engine, draconity_paused, (void*)"paused?", NULL, &cb_key);
    if (ret) draconity_logf("error adding paused callback: %d", ret);

    ret = _DSXEngine_SetBeginPhraseCallback(engine, draconity_phrase_begin, NULL, &cb_key);
    if (ret) draconity_logf("error setting phrase begin callback: %d", ret);

    draconity_set_default_params();

    printf("[+] status: start\n");
    fflush(stdout);
    draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("start")));
}

static void engine_acquire(drg_engine *engine, bool early) {
    if (!draconity->engine) {
        printf("[+] engine acquired\n");
        draconity->engine = engine;
        engine_setup(engine);
    }
    if (!early && !draconity->ready) {
        if (_DSXEngine_GetCurrentSpeaker(_engine)) {
            draconity_ready();
        }
    }
}

static int DSXEngine_SetBeginPhraseCallback() {
    draconity_logf("warning: called stubbed SetBeginPhraseCallback()\n");
    return 0;
}

static int DSXEngine_SetEndPhraseCallback() {
    draconity_logf("warning: called stubbed SetEndPhraseCallback()\n");
    return 0;
}

static int DSXFileSystem_PreferenceSetValue(drg_filesystem *fs, char *a, char *b, char *c, char *d) {
    // printf("DSXFileSystem_PreferenceSetValue(%p, %s, %s, %s, %s);\n", fs, a, b, c, d);
    return _DSXFileSystem_PreferenceSetValue(fs, a, b, c, d);
}

// track which dragon grammars are active, so "dragon" pseudogrammar can activate them
int (*orig_DSXGrammar_Activate)(drg_grammar *grammar, uintptr_t unk1, uintptr_t unk2, const char *rule_name);
int DSXGrammar_Activate(drg_grammar *grammar, uintptr_t unk1, uintptr_t unk2, const char *rule_name) {
    draconity->dragon_lock.lock();
    ForeignRule *foreign_rule = new ForeignRule(grammar, unk1, unk2, rule_name);
    draconity->dragon_rules.push_back(foreign_rule);
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = orig_DSXGrammar_Activate(grammar, unk1, unk2, rule_name);
    }
    draconity->dragon_lock.unlock();
    return ret;
}

int (*orig_DSXGrammar_Deactivate)(drg_grammar *grammar, uintptr_t unk1, const char *rule_name);
int DSXGrammar_Deactivate(drg_grammar *grammar, uintptr_t unk1, const char *rule_name) {
    draconity->dragon_lock.lock();
    // Remove the grammar from the draconity's internal map (if it exists).
    ForeignRule *rule_to_remove = NULL;
    for (ForeignRule *foreign_rule : draconity->dragon_rules) {
        if (foreign_rule->matches(grammar, rule_name)) {
            // Can't remove within the for loop - store the grammar, then break
            // and remove it.
            rule_to_remove = foreign_rule;
            break;
        }
    }
    if (rule_to_remove) {
        draconity->dragon_rules.remove(rule_to_remove);
        delete rule_to_remove;
    }

    // Now Draconity's record of the grammar has been removed, it can be
    // disabled.
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = orig_DSXGrammar_Deactivate(grammar, unk1, rule_name);
    }
    draconity->dragon_lock.unlock();
    return ret;
}

int (*orig_DSXGrammar_SetList)(drg_grammar *grammar, const char *name, dsx_dataptr *data);
int DSXGrammar_SetList(drg_grammar *grammar, const char *name, dsx_dataptr *data) {
    draconity->dragon_lock.lock();
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = orig_DSXGrammar_SetList(grammar, name, data);
    }
    draconity->dragon_lock.unlock();
    return ret;
}

/*
int DSXGrammar_RegisterEndPhraseCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterEndPhraseCallback(grammar, cb, user, key);
}

int DSXGrammar_RegisterBeginPhraseCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterBeginPhraseCallback(grammar, cb, user, key);
}

int DSXGrammar_RegisterPhraseHypothesisCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterPhraseHypothesisCallback(grammar, cb, user, key);
}
*/


extern "C" {
drg_engine *(*orig_DSXEngine_New)();
static drg_engine *DSXEngine_New() {
    drg_engine *engine = orig_DSXEngine_New();
    draconity_logf("DSXEngine_New() = %p", engine);
    engine_acquire(engine, true);
    return _engine;
}

int (*orig_DSXEngine_Create)(char *s, uintptr_t val, drg_engine **engine);
static int DSXEngine_Create(char *s, uintptr_t val, drg_engine **engine) {
    int ret = orig_DSXEngine_Create(s, val, engine);
    draconity_logf("DSXEngine_Create(%s, %llu, &%p) = %d", s, val, engine, ret);
    engine_acquire(*engine, true);
    return ret;
}

int (*orig_DSXEngine_GetMicState)(drg_engine *engine, int64_t *state);
static int DSXEngine_GetMicState(drg_engine *engine, int64_t *state) {
    engine_acquire(engine, false);
    return orig_DSXEngine_GetMicState(engine, state);
}

int (*orig_DSXEngine_SetMicState)(drg_engine *engine, int state, int unk1, int unk2);
static int DSXEngine_SetMicState(drg_engine *engine, int state, int unk1, int unk2) {
    engine_acquire(engine, false);
    return orig_DSXEngine_SetMicState(engine, state, unk1, unk2);
}

int (*orig_DSXEngine_LoadGrammar)(drg_engine *engine, int format, void *data, void **grammar);
static int DSXEngine_LoadGrammar(drg_engine *engine, int format, void *data, void **grammar) {
    engine_acquire(engine, false);
    return orig_DSXEngine_LoadGrammar(engine, format, data, grammar);
}

} // extern "C"

#define h(name) makeCodeHook(#name, name, &orig_##name)
static std::list<CodeHook> server_hooks {
    h(DSXEngine_New),
    h(DSXEngine_Create),
    h(DSXEngine_GetMicState),
    h(DSXEngine_SetMicState),
    h(DSXEngine_LoadGrammar),
};
#undef h

#define s(name) makeSymbolLoad(#name, &_##name)
static std::list<SymbolLoad> server_syms {
    s(DSXEngine_Create),
    s(DSXEngine_New),

    s(DSXEngine_AddWord),
    s(DSXEngine_AddTemporaryWord),
    s(DSXEngine_DeleteWord),
    s(DSXEngine_ValidateWord),
    s(DSXEngine_EnumWords),

    s(DSXWordEnum_GetCount),
    s(DSXWordEnum_Next),
    s(DSXWordEnum_End),

    s(DSXEngine_GetCurrentSpeaker),
    s(DSXEngine_SetMicState),
    s(DSXEngine_GetMicState),
    s(DSXEngine_LoadGrammar),
    s(DSXEngine_Mimic),
    s(DSXEngine_Pause),
    s(DSXEngine_RegisterAttribChangedCallback),
    s(DSXEngine_RegisterMimicDoneCallback),
    s(DSXEngine_RegisterPausedCallback),
    s(DSXEngine_Resume),
    s(DSXEngine_ResumeRecognition),
    s(DSXEngine_SetBeginPhraseCallback),
    s(DSXEngine_SetEndPhraseCallback),

    s(DSXEngine_SetStringValue),
    s(DSXEngine_GetValue),
    s(DSXEngine_GetParam),
    s(DSXEngine_DestroyParam),

    s(DSXFileSystem_PreferenceGetValue),
    s(DSXFileSystem_PreferenceSetValue),
    s(DSXFileSystem_SetResultsDirectory),
    s(DSXFileSystem_SetUsersDirectory),
    s(DSXFileSystem_SetVocabsLocation),

    s(DSXGrammar_Activate),
    s(DSXGrammar_Deactivate),
    s(DSXGrammar_Destroy),
    s(DSXGrammar_GetList),
    s(DSXGrammar_RegisterBeginPhraseCallback),
    s(DSXGrammar_RegisterEndPhraseCallback),
    s(DSXGrammar_RegisterPhraseHypothesisCallback),
    s(DSXGrammar_SetApplicationName),
    s(DSXGrammar_SetApplicationName),
    s(DSXGrammar_SetList),
    s(DSXGrammar_SetPriority),
    s(DSXGrammar_SetSpecialGrammar),
    s(DSXGrammar_Unregister),

    s(DSXResult_BestPathWord),
    s(DSXResult_GetWordNode),
    s(DSXResult_Destroy),
};
#undef s

extern int talon_draconity_install(std::list<SymbolLoad> server_syms, std::list<CodeHook> server_hooks);
void draconity_install() {
    printf("[+] draconity starting\n");
    int hooked = 0;
#ifdef TALON_BUILD
    hooked |= talon_draconity_install(server_syms, server_hooks);
#elif defined(__APPLE__)
    hooked |= Platform::loadSymbols("server.so", server_syms);
    hooked |= Platform::applyHooks("server.so", server_hooks);
#else
    hooked |= Platform::loadSymbols("server.dll", server_syms);
    hooked |= Platform::applyHooks("server.dll", server_hooks);
#endif
    if (hooked != 0) {
        printf("[!] draconity failed to hook!");
        return;
    }
    draconity_init();
}

auto _ = Draconity::shared();
