// #include "stdafx.h"
#include <string>
#include <list>
#include "libloaderapi.h"

#include "symbolicator.h"
#include "codehook.h"


static int load_symbols(HMODULE module, std::list<SymbolLoadBase*> symbol_loads) {
    for (SymbolLoadBase* symbol_load : symbol_loads) {
		// If symbol load fails, still continue. Don't interrupt Dragon.
        symbol_load->load(module);
    }

    for (SymbolLoadBase* symbol_load : symbol_loads) {
        if (!symbol_load->active) {
            printf("[!] Failed to load symbol %s\n", symbol_load->name);
        }
    }
    return 0;
}


int hook_codehooks(HMODULE module, std::list<CodeHookBase*> hooks) {
    for (CodeHookBase* hook : hooks) {
		// If hooking fails, still continue. Don't interrupt Dragon.
        hook->setup(module);
    }
    // Iterate twice to segregate failure messages
    for (CodeHookBase* hook : hooks) {
        if (!hook->active) {
            printf("[!] Failed to hook %s\n", hook->name);
        }
    }
    return S_OK;
}


/* Hook all functions and connect all symbols to Dragon */
int draconity_hook_symbols(std::list<CodeHookBase*> dragon_hooks, std::list<SymbolLoadBase*> server_syms) {
    HMODULE server = GetModuleHandleA("server.dll");
	if (server == 0) {
		printf("[!] Failed to load server.dll\n");
		return 1;
	}
    printf("[+] Loading symbols from server.dll\n");
    load_symbols(server, server_syms);
    printf("[+] Hooking code hooks in server.dll\n");
    hook_codehooks(server, dragon_hooks);
    return 0;
};
