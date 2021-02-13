#pragma once
#include <core/common.h>
#include <vm/vm.h>
#include <compiler/compiler.h>

void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t size);

void collectGarbage(VM* vm);

void freeObjects(VM* vm);

void markValue(VM* vm, Value value);
void markObject(VM* vm, Obj* object);
void markTable(VM* vm, Table* table);

#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define GROW_ARRAY(vm, type, pointer, oldSize, newSize) reallocate(vm, pointer, sizeof(type) * (oldSize), sizeof(type) * (newSize))

#define FREE(vm, type, pointer) reallocate(vm, pointer, sizeof(type), 0)
#define FREE_ARRAY(vm, type, pointer, length) reallocate(vm, pointer, sizeof(type) * (length), 0)