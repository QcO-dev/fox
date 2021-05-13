#include "list.h"
#include <vm/vm.h>
#include <vm/opcodes.h>
#include <stdio.h>

Value listLengthNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL((double)AS_LIST(*bound)->items.count);
}

Value listAppendNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	writeValueArray(vm, &AS_LIST(*bound)->items, args[0]);
	return NULL_VAL;
}

Value listIteratorNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ObjInstance* inst = newInstance(vm, vm->iteratorClass);

	tableSet(vm, &inst->fields, copyString(vm, "index", 5), NUMBER_VAL(0));

	tableSet(vm, &inst->fields, copyString(vm, "data", 4), *bound);

	return OBJ_VAL(inst);
}

void defineListMethods(VM* vm) {
	defineNative(vm, &vm->listMethods, "length", listLengthNative, 0, false);
	defineNative(vm, &vm->listMethods, "append", listAppendNative, 1, false);
	defineNative(vm, &vm->listMethods, "iterator", listIteratorNative, 0, false);
}