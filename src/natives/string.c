#include "string.h"
#include <vm/vm.h>

Value stringLengthNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL(AS_STRING(*bound)->length);
}

void defineStringMethods(VM* vm) {
	defineNative(vm, &vm->stringMethods, "length", stringLengthNative, 0);
}