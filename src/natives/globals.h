#pragma once
#include <vm/object.h>

void defineGlobalVariables(VM* vm);
void defineNative(VM* vm, Table* table, const char* name, NativeFn function, size_t arity);