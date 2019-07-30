#include <string>
#include <list>
#include "codehook.h"


class SymbolLoadBase {
public:
    SymbolLoadBase(const std::string name) {
        this->name = name;
        this->active = false;
    }

    virtual int load(HMODULE module) { return 1; };

    std::string name;
    bool active;
private:
};

template <typename F> class SymbolLoad : public SymbolLoadBase {
public:
    SymbolLoad(std::string name, F* ptr) : SymbolLoadBase(name) {
        this->ptr = ptr;
    }

    /* Load the symbol from `module` into this SymbolLoad */
    int load(HMODULE module) override {
        FARPROC ptr = GetProcAddress(module, this->name.c_str());
        if (ptr == 0) {
            // Failed to load the symbol
            return 1;
        } else {
            // Symbol was loaded successfully.
            //
            // Assign the underlying API function to the symbol we found.
            *this->ptr = reinterpret_cast<F>(ptr);
            this->active = true;
            return 0;
        }
    };

private:
    F* ptr;
};

int draconity_hook_symbols(std::list<CodeHookBase*> dragon_hooks,
                           std::list<SymbolLoadBase*> server_syms);
