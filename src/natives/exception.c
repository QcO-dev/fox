#include "exception.h"
#include <vm/vm.h>
#include <string.h>
#include <debug/disassemble.h>

Value exceptionInitializer(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ObjInstance* instance = AS_INSTANCE(*bound);

	tableSet(vm, &instance->fields, copyString(vm, "filename", 8), OBJ_VAL(copyString(vm, vm->filename, strlen(vm->filename))));

	CallFrame* frame = vm->frame;
	ObjFunction* function = frame->closure->function;

	size_t instruction = frame->ip - function->chunk.code - 1;

	size_t line = getLine(&function->chunk.table, instruction);

	tableSet(vm, &instance->fields, copyString(vm, "lineNumber", 10), NUMBER_VAL(line));

	return OBJ_VAL(instance);
}

void defineExceptionMethods(VM* vm, ObjClass* klass) {
	defineNative(vm, &klass->methods, "Exception", exceptionInitializer, 0, false);
}