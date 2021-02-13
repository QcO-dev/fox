#pragma once
#include <core/common.h>
#include <vm/vm.h>
#include <compiler/compiler.h>

void* reallocate(void* pointer, size_t oldSize, size_t size);

void setCurrentVMMemory(VM* vm); //TODO temp

void collectGarbage(VM* vm);

void freeObjects(VM* vm);

void markValue(VM* vm, Value value);
void markObject(VM* vm, Obj* object);
void markTable(VM* vm, Table* table);

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define GROW_ARRAY(type, pointer, oldSize, newSize) reallocate(pointer, sizeof(type) * (oldSize), sizeof(type) * (newSize))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)
#define FREE_ARRAY(type, pointer, length) reallocate(pointer, sizeof(type) * (length), 0)