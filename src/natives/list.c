#include "list.h"
#include <vm/vm.h>
#include <vm/opcodes.h>
#include <stdio.h>

Value listLengthNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL(AS_LIST(*bound)->items.count);
}

Value listAppendNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	writeValueArray(vm, &AS_LIST(*bound)->items, args[0]);
	return NULL_VAL;
}

void defineListMethods(VM* vm) {
	defineNative(vm, &vm->listMethods, "length", listLengthNative, 0);
	defineNative(vm, &vm->listMethods, "append", listAppendNative, 1);
}