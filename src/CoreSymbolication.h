#ifndef CORE_SYMBOLICATION_H
#define CORE_SYMBOLICATION_H

#include <mach/mach.h>

#define kCSNow 0x8000000000000000llu
typedef struct { void *data, *obj; } CSTypeRef;
typedef struct { uint64_t addr, size; } CSRange;
typedef CSTypeRef CSSymbolOwnerRef, CSSymbolRef, CSSymbolicatorRef;
typedef int (^CSSymbolIterator)(CSSymbolRef symbol);
typedef int (^CSSymbolOwnerIterator)(CSSymbolOwnerRef owner);

CSRange CSSymbolGetRange(CSSymbolRef sym);
const char *CSSymbolGetName(CSSymbolRef sym);

CSSymbolRef CSSymbolOwnerGetSymbolWithMangledName(CSSymbolOwnerRef owner, const char *name);
CSSymbolRef CSSymbolOwnerGetSymbolWithName(CSSymbolOwnerRef owner, const char *name);
const char *CSSymbolOwnerGetName(CSSymbolOwnerRef symbol);
const char *CSSymbolOwnerGetPath(CSSymbolOwnerRef symbol);
long CSSymbolOwnerForeachSymbol(CSSymbolOwnerRef owner, CSSymbolIterator each);
vm_address_t CSSymbolOwnerGetBaseAddress(CSSymbolOwnerRef owner);

CSSymbolicatorRef CSSymbolicatorCreateWithTask(task_t task);
long CSSymbolicatorForeachSymbolOwnerAtTime(CSSymbolicatorRef cs, uint64_t time, CSSymbolOwnerIterator it);
CSSymbolOwnerRef CSSymbolicatorGetSymbolOwnerWithNameAtTime(CSSymbolicatorRef cs, const char *name, uint64_t time);
long CSSymbolicatorForeachSymbolAtTime(CSSymbolicatorRef cs, uint64_t time, CSSymbolIterator it);

bool CSIsNull(CSTypeRef cs);
void CSRelease(CSTypeRef ref);

#endif
