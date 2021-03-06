#pragma once

#include <list>

#include "symbol_load.h"
#include "code_hook.h"

class Platform {
public:
    static size_t pageSize();
    static void *mmap(size_t size);
    static void munmap(void *addr, size_t size);
    static void protectRW(void *addr, size_t size);
    static void protectRX(void *addr, size_t size);
    static int loadSymbols(std::string module, std::list<SymbolLoad> &loads);
    static int applyHooks(std::string module, std::list<CodeHook> &hooks);
    static std::string homedir();
    static std::string expanduser(std::string path);
};
