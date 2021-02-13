#include "list.h"
#include <vm/vm.h>
#include <vm/opcodes.h>
#include <stdio.h>

Value listLengthNative(VM* vm, size_t argCount, Value* args, bool* hasError) {
	return NUMBER_VAL(AS_LIST(args[0])->items.count);
}

Value listAppendNative(VM* vm, size_t argCount, Value* args, bool* hasError) {
	writeValueArray(&AS_LIST(args[0])->items, args[1]);
	return NULL_VAL;
}

void defineListMethods(VM* vm) {
	defineNative(vm, &vm->listMethods, "length", listLengthNative, 0);
	defineNative(vm, &vm->listMethods, "append", listAppendNative, 1);
}