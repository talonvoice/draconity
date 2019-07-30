#pragma once
#include <string>

class SymbolLoad {
public:
    SymbolLoad(const std::string name, void **ptr) {
        this->name = name;
        this->loaded = false;
        this->ptr = ptr;
    }

    void setAddr(void *addr) {
        *this->ptr = addr;
        this->loaded = true;
    };
    std::string name;
    bool loaded;
private:
    void **ptr;
};

template <typename F>
SymbolLoad makeSymbolLoad(std::string name, F *ptr) {
    return SymbolLoad(name, reinterpret_cast<void **>(ptr));
}
