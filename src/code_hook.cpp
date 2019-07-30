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

int CodeHook::setup(void *addr) {
    printf("[+] hooking %s (%p)\n", this->name.c_str(), addr);
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

    *(this->original) = trampoline;
    this->active = true;
    return 0;
}

bool CodeHook::write(void *addr, void *data, size_t size) {
    Platform::protectRW(addr, size);
    memcpy(addr, data, size);
    Platform::protectRX(addr, size);
    return true;
}

#ifdef B64

void CodeHook::jmpPatch(uint8_t *buf, void *target) {
    uint8_t jmp[12] = { 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0 };
    *(uint64_t *)&jmp[2] = (uintptr_t)target;
    memcpy(buf, jmp, sizeof(jmp));
}
int CodeHook::jmpSize() { return 12; }

#else

void CodeHook::jmpPatch(uint8_t *buf, void *target) {
    uint8_t jmp[7] = { 0xb8, 0, 0, 0, 0, 0xff, 0xe0 };
    *(uint32_t *)&jmp[1] = (uintptr_t)target;
    memcpy(buf, jmp, sizeof(jmp));
}
int CodeHook::jmpSize() { return 7; }

#endif
