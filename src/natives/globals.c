#include "globals.h"
#include <vm/vm.h>
#include <core/file.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void defineNative(VM* vm, Table* table, const char* name, NativeFn function, size_t arity, bool varArgs) {
	push(vm, OBJ_VAL(copyString(vm, name, (int)strlen(name))));
	push(vm, OBJ_VAL(newNative(vm, function, arity, varArgs)));
	tableSet(vm, table, AS_STRING(vm->stack[0]), vm->stack[1]);
	pop(vm);
	pop(vm);
}

static Value clockNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value sqrtNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	if (!IS_NUMBER(args[0])) {
		//runtimeError(vm, "Expected first parameter to be a number.");
		*hasError = !throwException(vm, "TypeException", "Expected first parameter to be a number.");
		return pop(vm);
	}

	return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value inputNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	for (size_t i = 0; i < argCount; i++) {
		char* rep = valueToString(vm, args[i]);
		printf("%s", rep);
		free(rep);
		if (i != argCount - 1) printf(" ");
	}

	char* input = inputString(stdin, 20);
	return OBJ_VAL(takeString(vm, input, strlen(input)));
}

static Value readNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	if (!IS_STRING(args[0])) {
		*hasError = !throwException(vm, "TypeException", "Expected first parameter to be a string.");
		return pop(vm);
	}
	File file = readFile(AS_CSTRING(args[0]));
	if (file.isError) {
		*hasError = !throwException(vm, "IOException", file.contents);
		free(file.contents);
		return pop(vm);
	}
	return OBJ_VAL(takeString(vm, file.contents, strlen(file.contents)));
}

static Value printNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	for (size_t i = 0; i < argCount; i++) {
		char* rep = valueToString(vm, args[i]);
		printf("%s", rep);
		free(rep);
		if (i != argCount - 1) printf(" ");
	}
	printf("\n");

	return NULL_VAL;
}

void defineGlobalVariables(VM* vm) {
	defineNative(vm, &vm->globals, "clock", clockNative, 0, false);
	defineNative(vm, &vm->globals, "sqrt", sqrtNative, 1, false);
	defineNative(vm, &vm->globals, "input", inputNative, 0, true);
	defineNative(vm, &vm->globals, "read", readNative, 1, false);
	defineNative(vm, &vm->globals, "print", printNative, 0, true);
}