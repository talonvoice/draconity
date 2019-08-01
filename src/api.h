#ifndef DLAPI
#define DLAPI
#endif

// dragon 5
DLAPI drg_engine *(*_DSXEngine_New)();
// dragon 6
DLAPI int (*_DSXEngine_Create)(char *s, uintptr_t val, drg_engine **engine);

DLAPI int (*_DSXEngine_AddWord)(drg_engine *engine, const char *word, int flags, drg_wordinfo **info);
DLAPI int (*_DSXEngine_AddTemporaryWord)(drg_engine *engine, const char *word, int flags);
DLAPI int (*_DSXEngine_DeleteWord)(drg_engine *engine, int flags, const char *word);
DLAPI int (*_DSXEngine_ValidateWord)(drg_engine *engine, const char *word, bool *valid);
DLAPI drg_worditer *(*_DSXEngine_EnumWords)(drg_engine *engine, int flags);

DLAPI int (*_DSXWordEnum_GetCount)(drg_worditer *iter, uint32_t *count);
DLAPI int (*_DSXWordEnum_Next)(drg_worditer *iter, int max_words, char *buf, uint32_t *word_count, int buf_size, uint32_t *size_needed);
DLAPI int (*_DSXWordEnum_End)(drg_worditer *iter, void *dunno);

DLAPI int (*_DSXEngine_LoadGrammar)(drg_engine *engine, int type, dsx_dataptr *data, drg_grammar **grammar_out);
DLAPI void *(*_DSXEngine_GetCurrentSpeaker)(drg_engine *engine);
DLAPI int (*_DSXEngine_GetMicState)(drg_engine *engine, int64_t *state);
DLAPI int (*_DSXEngine_Mimic)(drg_engine *engine, int unk1, unsigned int count, dsx_dataptr *data, unsigned int unk2, int type);
DLAPI int (*_DSXEngine_RegisterAttribChangedCallback)(drg_engine *engine,  void (*cb)(int, dsx_attrib*), void *user, unsigned int *key);
DLAPI int (*_DSXEngine_RegisterMimicDoneCallback)(drg_engine *engine, void (*cb)(int, dsx_mimic*), void *user, unsigned int *key);
DLAPI int (*_DSXEngine_RegisterPausedCallback)(drg_engine *engine, void (*cb)(int, dsx_paused*), void *user, char *name, unsigned int *key);

DLAPI int (*_DSXEngine_SetStringValue)(drg_engine *engine, void *param, const char *value);
DLAPI void *(*_DSXEngine_GetValue)(drg_engine *engine, void *param, void **type, void *value_out, unsigned int size, unsigned int *size_out);
DLAPI void *(*_DSXEngine_GetParam)(drg_engine *engine, const char *key);
DLAPI void (*_DSXEngine_DestroyParam)(drg_engine *engine, void *param);

DLAPI int (*_DSXEngine_SetBeginPhraseCallback)(drg_engine *engine, int (*cb)(void*, void*), void *user, unsigned int *key);
DLAPI int (*_DSXEngine_SetEndPhraseCallback)(drg_engine *engine, void *cb, void *user, unsigned int *key);

DLAPI int (*_DSXEngine_Pause)(drg_engine *engine);
DLAPI int (*_DSXEngine_Resume)(drg_engine *engine, uintptr_t token);
DLAPI int (*_DSXEngine_ResumeRecognition)(drg_engine *engine);

DLAPI int (*_DSXFileSystem_PreferenceSetValue)(drg_filesystem *fs, char *a, char *b, char *c, char *d);
DLAPI int (*_DSXFileSystem_PreferenceGetValue)(drg_filesystem *fs, char *a, char *b, char *c, char *d);

DLAPI int (*_DSXFileSystem_SetUsersDirectory)(drg_filesystem *fs, char *a, bool unk);
DLAPI int (*_DSXFileSystem_SetVocabsLocation)(drg_filesystem *fs, char *a, bool unk);
DLAPI int (*_DSXFileSystem_SetResultsDirectory)(drg_filesystem *fs, char *a, bool unk);

DLAPI int (*_DSXGrammar_Activate)(drg_grammar *grammar, uintptr_t unk1, bool unk2, const char *main_rule);
DLAPI int (*_DSXGrammar_Deactivate)(drg_grammar *grammar, uintptr_t unk1, const char *main_rule);
DLAPI int (*_DSXGrammar_Destroy)(drg_grammar *);
DLAPI int (*_DSXGrammar_GetList)(drg_grammar *grammar, const char *name, dsx_dataptr *data);
DLAPI int (*_DSXGrammar_RegisterBeginPhraseCallback)(drg_grammar *grammar, int (*cb)(void *, void *), void *user, unsigned int *key);
DLAPI int (*_DSXGrammar_RegisterEndPhraseCallback)(drg_grammar *grammar, int (*cb)(void *, dsx_end_phrase *), void *user, unsigned int *key);
DLAPI int (*_DSXGrammar_RegisterPhraseHypothesisCallback)(drg_grammar *grammar, int (*cb)(void *, dsx_end_phrase *), void *user, unsigned int *key);
DLAPI int (*_DSXGrammar_SetApplicationName)(drg_grammar *grammar, const char *name);
DLAPI int (*_DSXGrammar_SetList)(drg_grammar *grammar, const char *name, dsx_dataptr *data);
DLAPI int (*_DSXGrammar_GetApplicationName)(drg_grammar *grammar, char *buf, int buf_size, int *size_out);
DLAPI int (*_DSXGrammar_SetPriority)(drg_grammar *grammar, int priority);
DLAPI int (*_DSXGrammar_SetSpecialGrammar)(drg_grammar *grammar, int special);
DLAPI int (*_DSXGrammar_Unregister)(drg_grammar *grammar, unsigned int key);

DLAPI int (*_DSXResult_BestPathWord)(dsx_result *result, int choice, uint32_t *path, size_t pathSize, size_t *needed);
DLAPI int (*_DSXResult_GetWordNode)(dsx_result *result, uint32_t path, void *node, uint32_t *num, char **name);
DLAPI int (*_DSXResult_Destroy)(dsx_result *result);

DLAPI void (*_SDApi_SetShowCalls)(bool show);
DLAPI void (*_SDApi_SetShowCallsWithFileSpecArgs)(bool show);
DLAPI void (*_SDApi_SetShowCallPointerArguments)(bool show);
DLAPI void (*_SDApi_SetShowCallMemDeltas)(bool show);
DLAPI void (*_SDApi_SetShowAllocation)(bool show);
DLAPI void (*_SDApi_SetShowAllocationHistogram)(bool show);

DLAPI void *(*_SDRule_New)(void *sdapi, void *rule);
DLAPI void *(*_SDRule_Delete)(void *sdapi, void *rule);

#undef DLAPI
