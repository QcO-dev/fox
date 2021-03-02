#include "globals.h"
#include <vm/vm.h>
#include <core/file.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void defineNative(VM* vm, Table* table, const char* name, NativeFn function, size_t arity) {
	push(vm, OBJ_VAL(copyString(vm, name, (int)strlen(name))));
	push(vm, OBJ_VAL(newNative(vm, function, arity)));
	tableSet(vm, table, AS_STRING(vm->stack[0]), vm->stack[1]);
	pop(vm);
	pop(vm);
}

static Value clockNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value sqrtNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	if (!IS_NUMBER(args[0])) {
		runtimeError(vm, "Expected first parameter to be a number.\nin sqrt");
		*hasError = true;
		return NULL_VAL;
	}

	return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value inputNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	char* input = inputString(stdin, 20);
	return OBJ_VAL(takeString(vm, input, strlen(input)));
}

static Value readNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	if (!IS_STRING(args[0])) {
		runtimeError(vm, "Expected first parameter to be a string.\nin read");
		*hasError = true;
		return NULL_VAL;
	}
	char* contents = readFile(AS_CSTRING(args[0]));
	if (contents == NULL) {
		runtimeError(vm, "Could not open file '%s'\nin read", AS_CSTRING(args[0]));
		*hasError = true;
		return NULL_VAL;
	}
	return OBJ_VAL(takeString(vm, contents, strlen(contents)));
}

static Value printNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	char* rep = valueToString(vm, args[0]);
	printf("%s\n", rep);
	free(rep);
	return NULL_VAL;
}

void defineGlobalVariables(VM* vm) {
	defineNative(vm, &vm->globals, "clock", clockNative, 0);
	defineNative(vm, &vm->globals, "sqrt", sqrtNative, 1);
	defineNative(vm, &vm->globals, "input", inputNative, 0);
	defineNative(vm, &vm->globals, "read", readNative, 1);
	defineNative(vm, &vm->globals, "print", printNative, 1);
}