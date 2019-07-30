#pragma once

#include <string>

class CodeHook {
 public:
    CodeHook(std::string name) {
        this->name = name;
        this->active = false;
    }
    virtual int setup(uintptr_t addr);

public:
    std::string name;
    bool active;
};

template <typename F>
CodeHook makeCodeHook(std::string name, F target, F *original);
