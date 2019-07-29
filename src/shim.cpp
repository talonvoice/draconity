// TODO needs to be replaced with cross platform shimming approach

#include <Zydis/Zydis.h>
#include <bson.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "draconity.h"

#ifndef streq
#define streq(a, b) !strcmp(a, b)
#endif

// #define DEBUG

#if CPP_PORT_IS_DONE // unclear whether draconity.h's _engine macro usage here is intentional - suspect not though
drg_engine *_engine = NULL;
#endif //CPP_PORT_IS_DONE


#include "api.h" // this defines the function pointers


#if RUN_IN_DRAGON
int draconity_set_param(const char *key, const char *value) {
    if (!_engine) return -1;
    void *param = _DSXEngine_GetParam(_engine, key);
    int ret = _DSXEngine_SetStringValue(_engine, param, value);
    _DSXEngine_DestroyParam(_engine, param);
    return ret;
}

static char *homedir() {
    char *home = getenv("HOME");
    if (home) {
        return strdup(home);
    } else {
        struct passwd pw, *pwp;
        char buf[1024];
        if (getpwuid_r(getuid(), &pw, buf, sizeof(buf), &pwp) == 0) {
            return strdup(pwp->pw_dir);
        }
        return NULL;
    }
}

typedef struct {
    char *timeout;
    char *timeout_incomplete;
} config;

static bool prevent_wake = false;

void draconity_set_default_params() {
    config config = {
        .timeout = strdup("80"),
        .timeout_incomplete = strdup("500"),
    };

    draconity_set_param("DwTimeOutComplete", config.timeout);
    draconity_set_param("DwTimeOutIncomplete", config.timeout_incomplete);
    free(config.timeout);
    free(config.timeout_incomplete);
    memset(&config, 0, sizeof(config));

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
    // CrashOnSDAPIError
}

static void engine_setup(drg_engine *engine) {
    // _SDApi_SetShowCalls(true);
    // _SDApi_SetShowCallsWithFileSpecArgs(true);
    // _SDApi_SetShowCallPointerArguments(true);
    // _SDApi_SetShowCallMemDeltas(true);
    // _SDApi_SetShowAllocation(true);
    // _SDApi_SetShowAllocationHistogram(true);

    static unsigned int cb_key = 0;
    int ret = _DSXEngine_RegisterAttribChangedCallback(engine, draconity_attrib_changed, NULL, &cb_key);
    if (ret) draconity_logf("error adding attribute callback: %d", ret);

    ret = _DSXEngine_RegisterMimicDoneCallback(engine, draconity_mimic_done, NULL, &cb_key);
    if (ret) draconity_logf("error adding mimic done callback: %d", ret);

    ret = _DSXEngine_RegisterPausedCallback(engine, draconity_paused, "paused?", NULL, &cb_key);
    if (ret) draconity_logf("error adding paused callback: %d", ret);

    ret = _DSXEngine_SetBeginPhraseCallback(engine, draconity_phrase_begin, NULL, &cb_key);
    if (ret) draconity_logf("error setting phrase begin callback: %d", ret);

    draconity_set_default_params();

    printf("[+] status: start\n");
    fflush(stdout);
    draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("start")));
}

static void engine_acquire(drg_engine *engine, bool early) {
    if (!_engine) {
        printf("[+] engine acquired\n");
        _engine = engine;
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
int DSXGrammar_Activate(drg_grammar *grammar, uint64_t unk1, bool unk2, const char *main_rule) {
    pthread_mutex_lock(draconity->dragon_lock);
    foreign_grammar *fg = malloc(sizeof(foreign_grammar));
    fg->grammar = grammar;
    fg->unk1 = unk1;
    fg->unk2 = unk2;
    if (main_rule) fg->main_rule = strdup(main_rule);
    else           fg->main_rule = NULL;
    draconity->dragon_grammars.push_back(fg);
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = _DSXGrammar_Activate(grammar, unk1, unk2, main_rule);
    }
    pthread_mutex_unlock(draconity->dragon_lock);
    return ret;
}

int DSXGrammar_Deactivate(drg_grammar *grammar, uint64_t unk1, const char *main_rule) {
    pthread_mutex_lock(draconity->dragon_lock);
    for (foreign_grammar *fg : draconity->dragon_grammars) {
        if (fg->grammar == grammar && ((fg->main_rule == NULL && main_rule == NULL)
                    || (fg->main_rule && main_rule && strcmp(fg->main_rule, main_rule) == 0))) {
            tack_remove(draconity->dragon_grammars, fg);
            free((void *)fg->main_rule);
            free(fg);
            break;
        }
    }
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = _DSXGrammar_Deactivate(grammar, unk1, main_rule);
    }
    pthread_mutex_unlock(draconity->dragon_lock);
    return ret;
}

int DSXGrammar_SetList(drg_grammar *grammar, const char *name, dsx_dataptr *data) {
    pthread_mutex_lock(draconity->dragon_lock);
    int ret = 0;
    if (draconity->dragon_enabled) {
        ret = _DSXGrammar_SetList(grammar, name, data);
    }
    pthread_mutex_unlock(draconity->dragon_lock);
    return ret;
}
#endif //RUN_IN_DRAGON

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


typedef struct {
    const char *name;
    void **ptr;
    bool active;
} symload;

#define h(_name) {.handle=NULL, .name=#_name, .target=_name, .offset=0, .active=false, 0}
#define ho(_name, _handle) {.handle=_handle, .name=#_name, .target=_name, .offset=0, .active=false, 0}
static code_hook dragon_hooks[] = {
#if RUN_IN_DRAGON
    ho(DSXEngine_New, &DSXEngine_New_hook),
    ho(DSXEngine_Create, &DSXEngine_Create_hook),
    ho(DSXEngine_GetMicState, &DSXEngine_GetMicState_hook),
    ho(DSXEngine_LoadGrammar, &DSXEngine_LoadGrammar_hook),
#endif //RUN_IN_DRAGON
    {0},
};
#undef h

#define s(x) {#x, (void **)&_##x, false}
static symload server_syms[] = {
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
    {0},
};

static symload mrec_syms[] = {
    s(SDApi_SetShowCalls),
    s(SDApi_SetShowCallsWithFileSpecArgs),
    s(SDApi_SetShowCallPointerArguments),
    s(SDApi_SetShowCallMemDeltas),
    s(SDApi_SetShowAllocation),
    s(SDApi_SetShowAllocationHistogram),
    s(SDRule_New),
    s(SDRule_Delete),
    {0},
};
#undef s

static void *draconity_install(void *_) {
    printf("[+] draconity starting\n");
    draconity_init();
    return NULL;
}

__attribute__((constructor))
void cons() {
#ifdef DEBUG
    int log = open("/tmp/draconity.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    dup2(log, 1);
    dup2(log, 2);
#endif
    draconity_install(NULL);
}