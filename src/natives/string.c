#include "string.h"
#include <vm/vm.h>

Value stringLengthNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL((double)AS_STRING(*bound)->length);
}

Value stringIteratorNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ObjInstance* inst = newInstance(vm, vm->iteratorClass);

	tableSet(vm, &inst->fields, copyString(vm, "index", 5), NUMBER_VAL(0));

	tableSet(vm, &inst->fields, copyString(vm, "data", 4), *bound);

	return OBJ_VAL(inst);
}

void defineStringMethods(VM* vm) {
	defineNative(vm, &vm->stringMethods, "length", stringLengthNative, 0, false);
	defineNative(vm, &vm->stringMethods, "iterator", stringIteratorNative, 0, false);
}