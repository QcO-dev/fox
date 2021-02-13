#include "globals.h"
#include <vm/vm.h>

#include <string.h>
#include <time.h>
#include <math.h>

void defineNative(VM* vm, Table* table, const char* name, NativeFn function, size_t arity) {
	push(vm, OBJ_VAL(copyString(vm, name, (int)strlen(name))));
	push(vm, OBJ_VAL(newNative(vm, function, arity)));
	tableSet(table, AS_STRING(vm->stack[0]), vm->stack[1]);
	pop(vm);
	pop(vm);
}

static Value clockNative(VM* vm, size_t argCount, Value* args, bool* hasError) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value sqrtNative(VM* vm, size_t argCount, Value* args, bool* hasError) {
	if (!IS_NUMBER(args[0])) {
		runtimeError(vm, "Expected first parameter to be a number.\nin sqrt");
		*hasError = true;
		return NULL_VAL;
	}

	return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

void defineGlobalVariables(VM* vm) {
	defineNative(vm, &vm->globals, "clock", clockNative, 0);
	defineNative(vm, &vm->globals, "sqrt", sqrtNative, 1);
}