#pragma once

#include <string>
#include <windows.h>
#include <stdint.h>

#include <Zydis/Zydis.h>


static size_t dis_code_size(uint8_t *addr, size_t size) {
    ZydisDecoder dis;
    ZydisDecoderInit(&dis, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);
    size_t offset = 0;
    ZydisDecodedInstruction ins;
    while (ZYDIS_SUCCESS(ZydisDecoderDecodeBuffer(&dis, addr + offset, 64, (uint64_t)addr + offset, &ins)) && offset < size) {
        offset += ins.length;
    }
    return offset;
}

static void dis_mem(uint8_t *addr, size_t size) {
    ZydisDecoder dis;
    ZydisDecoderInit(&dis, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);
    ZydisFormatter dis_fmt;
    ZydisFormatterInit(&dis_fmt, ZYDIS_FORMATTER_STYLE_INTEL);
    size_t offset = 0;
    ZydisDecodedInstruction ins;
    char buf[256];
    while (ZYDIS_SUCCESS(ZydisDecoderDecodeBuffer(&dis, addr + offset, size - offset, (uint64_t)addr + offset, &ins))) {
        ZydisFormatterFormatInstruction(&dis_fmt, &ins, buf, sizeof(buf));
        offset += ins.length;
    }
}

/* Base class exposing the minimal interface needed to attach the hook. */
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
class TypedCodeHook : public CodeHookBase {
public:
    TypedCodeHook(std::string name, F target, F *original) : CodeHookBase(name) {
        this->target = target;
        this->origData = (uint8_t *)calloc(1, jmpSize());
        this->patchData = (uint8_t *)calloc(1, jmpSize());
        this->original = original;
        this->active = false;
    }

    int setup(uintptr_t addr) override {
        printf("[+] hooking %s (%p)\n", this->name, addr);
        this->addr = addr;

        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        size_t page_mask = system_info.dwPageSize - 1;

        int paddedSize = dis_code_size((uint8_t*)addr, jmpSize());
        size_t trampolineSize = (paddedSize + jmpSize() + page_mask) & ~page_mask;
        uint8_t *trampoline = (uint8_t*)VirtualAlloc(NULL, trampolineSize, MEM_COMMIT, PAGE_READWRITE);

        memcpy(trampoline, addr, paddedSize);
        jmpPatch(trampoline + paddedSize, (uint8_t*)addr + paddedSize);
        DWORD oldProtect;
        VirtualProtect(trampoline, trampolineSize, PAGE_EXECUTE_READ, &oldProtect);

        jmpPatch(patchData, reinterpret_cast<uint8_t*>(target));
        memcpy(origData, addr, jmpSize());
        apply();

        *(this->original) = reinterpret_cast<F>(trampoline);
        this->active = true;
    }
protected:
	virtual void jmpPatch(uint8_t *buf, void *target) {};
	virtual int jmpSize() { return 1; };
private:
    bool apply() {
        return this->write(this->addr, this->patchData, this->jmpSize());
    }
    bool revert() {
        return this->write(this->addr, this->origData, this->jmpSize());
    }
    bool write(void *addr, void *data, int size) {
        DWORD old_protect;
        VirtualProtect(addr, size, PAGE_READWRITE, &old_protect);
        memcpy(addr, data, size);
        VirtualProtect(addr, size, old_protect, &old_protect);
        return true;
    }
public:
	void *addr;
private:
	F target;
	F *original;
	uint8_t *patchData, *origData;
};

template <typename F>
class CodeHook_x86 : public TypedCodeHook<F> {
public:
    CodeHook_x86(std::string name, F target, F *original)
        : TypedCodeHook<F>(name, target, original) {};
private:
	void jmpPatch(uint8_t *buf, void *target) override {
        uint8_t jmp[7] = { 0xb8, 0, 0, 0, 0, 0xff, 0xe0 };
        *(uint32_t *)&jmp[1] = (uintptr_t)target;
        memcpy(buf, jmp, sizeof(jmp));
    }
	int jmpSize() { return 7; }
};

template <typename F>
class CodeHook_x86_64 : public TypedCodeHook<F> {
public:
    CodeHook_x86_64(std::string name, F target, F *original)
        : TypedCodeHook<F>(name, target, original) {};
private:
    void jmpPatch(uint8_t *buf, void *target) override {
        uint8_t jmp[12] = { 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0 };
        *(uint64_t *)&jmp[2] = (uintptr_t)target;
        memcpy(buf, jmp, sizeof(jmp));
    }
	int jmpSize() override { return 12; }
};

template <typename F>
CodeHook<F> makeCodeHook(std::string name, F target, F *original) {
#ifdef B64
    return new CodeHook_x86_64<F>(name, target, original);
#else
    return new CodeHook_x86<F>(name, target, original);
#endif
}
