#include <Zydis/Zydis.h>
#include <string>

#include "platform.h"
#include "code_hook.h"

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

template <typename F>
class TypedCodeHook : public CodeHook {
public:
    TypedCodeHook(std::string name, F target, F *original) : CodeHook(name) {
        this->target = target;
        this->origData = (uint8_t *)calloc(1, jmpSize());
        this->patchData = (uint8_t *)calloc(1, jmpSize());
        this->original = original;
        this->active = false;
    }

    int setup(uintptr_t addr) override {
        printf("[+] hooking %s (%p)\n", this->name, addr);
        this->addr = addr;

        size_t page_mask = Platform::pageSize() - 1;

        int paddedSize = dis_code_size((uint8_t*)addr, jmpSize());
        size_t trampolineSize = (paddedSize + jmpSize() + page_mask) & ~page_mask;
        uint8_t *trampoline = (uint8_t *)Platform::mmap(trampolineSize);

        memcpy(reinterpret_cast<void *>(trampoline), reinterpret_cast<void *>(addr), paddedSize);
        jmpPatch(trampoline + paddedSize, (uint8_t*)addr + paddedSize);
        Platform::protectRX(trampoline, trampolineSize);

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
        Platform::protectRW(addr, size);
        memcpy(addr, data, size);
        Platform::protectRX(addr, size);
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
CodeHook makeCodeHook(std::string name, F target, F *original) {
#ifdef B64
    return CodeHook_x86_64<F>(name, target, original);
#else
    return CodeHook_x86<F>(name, target, original);
#endif
}
