#include "platform.h"

#ifdef __APPLE__

#include <unistd.h>
#include <sys/mman.h>

size_t Platform::pageSize() {
    return getpagesize();
}

void *Platform::mmap(size_t size) {
    void *addr = ::mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED)
        return NULL;
    return addr;
}

void Platform::munmap(void *addr, size_t size) {
    munmap(addr, size);
}

void Platform::protectRW(void *addr, size_t size) {
    mprotect(addr, size, PROT_READ|PROT_WRITE);
}

void Platform::protectRX(void *addr, size_t size) {
    mprotect(addr, size, PROT_READ|PROT_EXEC);
}

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
    VirtualProtect(addr, size, PAGE_EXECUTE_READ, &oldProtect);
}

void Platform::protectRX(void *addr, size_t size) {
    mprotect(addr, size, PROT_READ|PROT_EXEC);
}

int Platform::loadSymbols(std::string moduleName, std::list<SymbolLoad> loads) {
    HMODULE module = GetModuleHandleA(moduleName.c_str());
    if (!module) {
        printf("[!] Failed to open module %s\n", moduleName.c_str());
        return 1;
    }
    for (auto symbol_load : loads) {
        symbol_load->load(module);
    }
    for (auto symbol_load : loads) {
        if (!symbol_load->loaded) {
            printf("[!] Failed to load symbol %s\n", symbol_load->name.c_str());
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
        hook->setup(module);
    }
    for (auto hook : hooks) {
        if (!hook->active) {
            printf("[!] Failed to hook %s\n", hook->name.c_str());
        }
    }
    return 0;
};

#endif
