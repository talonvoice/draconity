#include "platform.h"

#ifdef __APPLE__

#include <unistd.h>
#include <sys/mman.h>
extern "C" {
#include "CoreSymbolication.h"
}

size_t Platform::pageSize() {
    return getpagesize();
}

void *Platform::mmap(size_t size) {
    void *addr = ::mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED)
        return NULL;
    return addr;
}

void Platform::munmap(void *addr, size_t size) {
    munmap(addr, size);
}

void Platform::protectRW(void *addr, size_t size) {
    uint64_t page_mask = pageSize() - 1;
    void *prot_addr = (void *)((uintptr_t)addr &~page_mask);
    size_t prot_size = (size + page_mask) & ~page_mask;
    mprotect(prot_addr, prot_size, PROT_READ|PROT_WRITE);
}

void Platform::protectRX(void *addr, size_t size) {
    uint64_t page_mask = pageSize() - 1;
    void *prot_addr = (void *)((uintptr_t)addr &~page_mask);
    size_t prot_size = (size + page_mask) & ~page_mask;
    mprotect(prot_addr, prot_size, PROT_READ|PROT_EXEC);
}

static int walk_image(std::string image, std::function<void(std::string, void *, size_t)> cb) {
    CSSymbolicatorRef csym = CSSymbolicatorCreateWithTask(mach_task_self());
    CSSymbolOwnerRef owner = CSSymbolicatorGetSymbolOwnerWithNameAtTime(csym, image.c_str(), kCSNow);
    if (CSIsNull(owner)) {
        printf("  [!] IMAGE NOT FOUND: %s\n", image.c_str());
        CSRelease(csym);
        return 1;
    }
    CSSymbolOwnerForeachSymbol(owner, ^int (CSSymbolRef sym) {
        const char *name = CSSymbolGetName(sym);
        if (name) {
            CSRange range = CSSymbolGetRange(sym);
            cb(name, (void *)range.addr, range.size);
            return 0;
        }
        return 1;
    });
    CSRelease(csym);
    return 0;
}

int Platform::loadSymbols(std::string moduleName, std::list<SymbolLoad> loads) {
    walk_image(moduleName, [&loads](std::string name, void *addr, size_t size) {
        for (auto &symbol_load : loads) {
            if (name == symbol_load.name) {
                symbol_load.setAddr(addr);
            }
        }
    });
    for (auto &symbol_load : loads) {
        if (!symbol_load.loaded) {
            printf("[!] Failed to load symbol %s\n", symbol_load.name.c_str());
        }
    }
    return 0;
};

int Platform::applyHooks(std::string moduleName, std::list<CodeHook> hooks) {
    walk_image(moduleName, [&hooks](std::string name, void *addr, size_t size) {
        for (auto &hook : hooks) {
            if (name == hook.name) {
                hook.setup(addr);
            }
        }
    });
    for (auto &hook : hooks) {
        if (!hook.active) {
            printf("[!] Failed to hook %s\n", hook.name.c_str());
        }
    }
    return 0;
};

#else // windows

#include <windows.h>

size_t Platform::pageSize() {
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}

void *Platform::mmap(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
}

void Platform::munmap(void *addr, size_t size) {
    VirtualFree(addr, size, MEM_RELEASE);
}

void Platform::protectRW(void *addr, size_t size) {
    DWORD oldProtect;
    VirtualProtect(addr, size, PAGE_READWRITE, &oldProtect);
}

void Platform::protectRX(void *addr, size_t size) {
    DWORD oldProtect;
    VirtualProtect(addr, size, PAGE_EXECUTE_READ, &oldProtect);
}

int Platform::loadSymbols(std::string moduleName, std::list<SymbolLoad> loads) {
    HMODULE module = GetModuleHandleA(moduleName.c_str());
    if (!module) {
        printf("[!] Failed to open module %s\n", moduleName.c_str());
        return 1;
    }
    for (auto symbol_load : loads) {
        symbol_load.load(module);
    }
    for (auto symbol_load : loads) {
        if (!symbol_load.loaded) {
            printf("[!] Failed to load symbol %s\n", symbol_load.name.c_str());
        }
    }
    return 0;
};

int Platform::applyHooks(std::string moduleName, std::list<CodeHook> hooks) {
    HMODULE module = GetModuleHandleA(moduleName.c_str());
    if (!module) {
        printf("[!] Failed to open module %s\n", moduleName.c_str());
        return 1;
    }
    for (auto hook : hooks) {
        hook.setup(module);
    }
    for (auto hook : hooks) {
        if (!hook.active) {
            printf("[!] Failed to hook %s\n", hook.name.c_str());
        }
    }
    return 0;
};

#endif
