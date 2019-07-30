#pragma once

#include <string>

class CodeHook {
public:
    CodeHook(std::string name, void *target, void **original) {
        this->name = name;
        this->active = false;
        this->target = target;
        this->original = original;
        this->origData = (uint8_t *)calloc(1, jmpSize());
        this->patchData = (uint8_t *)calloc(1, jmpSize());
    }
    CodeHook(const CodeHook &obj) {
        this->name = obj.name;
        this->active = obj.active;
        this->target = obj.target;
        this->original = obj.original;
        this->origData = (uint8_t *)calloc(1, jmpSize());
        this->patchData = (uint8_t *)calloc(1, jmpSize());
    }
    ~CodeHook() {
        free(origData);
        free(patchData);
    }
    int setup(void *addr);
private:
    CodeHook& operator=(const CodeHook &obj);
    bool apply() {
        return this->write(this->addr, this->patchData, this->jmpSize());
    }
    bool revert() {
        return this->write(this->addr, this->origData, this->jmpSize());
    }
    bool write(void *addr, void *data, size_t size);

    void jmpPatch(uint8_t *buf, void *target);
    int jmpSize();

public:
    std::string name;
    bool active;
private:
    void *addr;
    void *target;
    void **original;
    uint8_t *patchData, *origData;
};

template <typename F>
CodeHook makeCodeHook(std::string name, F target, F *original) {
    return CodeHook(name, reinterpret_cast<void *>(target), reinterpret_cast<void **>(original));
}
