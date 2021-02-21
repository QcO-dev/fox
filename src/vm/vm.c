#include "vm.h"
#include <core/common.h>
#include <core/memory.h>
#include <core/file.h>
#include <vm/opcodes.h>
#include <compiler/compiler.h>
#include <debug/debugFlags.h>
#include <debug/disassemble.h>
#include <vm/object.h>
#include <natives/globals.h>
#include <natives/list.h>
#include <natives/string.h>
#include <natives/objectNative.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <io.h>

static InterpreterResult import(VM* vm, char* path, ObjString* name, Value* value);

void initVM(VM* vm) {
	vm->stackTop = vm->stack;
	vm->objects = NULL;
	vm->frameCount = 0;
	vm->openUpvalues = NULL;
	vm->grayCount = 0;
	vm->grayCapacity = 0;
	vm->grayStack = NULL;
	vm->compiler = NULL;
	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;
	vm->basePath = NULL;
	vm->filename = NULL;
	vm->imports = NULL;
	vm->importCapacity = 0;
	vm->importCount = 0;
	vm->isImport = false;

	initTable(&vm->globals);
	initTable(&vm->exports);
	initTable(&vm->strings);
	initTable(&vm->listMethods);
	initTable(&vm->stringMethods);

	ObjClass* objectClass = newClass(vm, copyString(vm, "<object>", 8));
	vm->objectClass = objectClass;
	defineObjectMethods(vm, vm->objectClass);

	ObjClass* importClass = newClass(vm, copyString(vm, "<import>", 8));
	vm->importClass = importClass;
	defineObjectMethods(vm, vm->importClass);

	tableSet(vm, &vm->globals, copyString(vm, "Object", 6), OBJ_VAL(vm->objectClass));

	defineGlobalVariables(vm);
	defineListMethods(vm);
	defineStringMethods(vm);
}

void resetVM(VM* vm) {
	vm->stackTop = vm->stack;
	vm->frameCount = 0;
}

void push(VM* vm, Value value) {
	*vm->stackTop = value;
	vm->stackTop++;
}

Value pop(VM* vm) {
	if (vm->stackTop == vm->stack) return NULL_VAL;
	vm->stackTop--;
	return *vm->stackTop;
}

Value peek(VM* vm, int distance) {
	return vm->stackTop[-1 - distance];
}

void runtimeError(VM* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	fprintf(stderr, "In File '%s':\n", vm->filename);

	for (int i = vm->frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm->frames[i];
		ObjFunction* function = frame->closure->function;
		// -1 because the IP is sitting on the next instruction to be executed.
		size_t instruction = frame->ip - function->chunk.code - 1;
		fprintf(stderr, "[%d] in ", getLine(&function->chunk.table, instruction));
		if (function->name == NULL) {
			fprintf(stderr, "<script>\n");
		}
		else {
			fprintf(stderr, "%s\n", function->name->chars);
		}
	}

	vm->stackTop = vm->stack;
}

static inline void concat(VM* vm, ObjString* a, ObjString* b) {
	size_t length = a->length + b->length;
	char* chars = ALLOCATE(vm, char, length + 1);
	memcpy(chars, a->chars, a->length); // Copy a
	memcpy(chars + a->length, b->chars, b->length); // Copy b
	chars[length] = '\0'; // Terminate

	ObjString* result = takeString(vm, chars, length);
	pop(vm);
	pop(vm);
	push(vm, OBJ_VAL(result));
}

static void concatenate(VM* vm, bool firstString, bool secondString) {
	
	ObjString* a;
	ObjString* b;
	
	if (firstString && secondString) {

		b = AS_STRING(peek(vm, 0));
		a = AS_STRING(peek(vm, 1));

		concat(vm, a, b);

	}
	else if (firstString) {
		b = AS_STRING(peek(vm, 0));
		Value aValue = peek(vm, 1);
		// This breaks the GC
		char* aChars = valueToString(aValue);
		a = copyString(vm, aChars, strlen(aChars));
		free(aChars);
		concat(vm, a, b);
	}
	else {
		Value bValue = peek(vm, 0);
		char* bChars = valueToString(bValue);
		b = copyString(vm, bChars, strlen(bChars));
		free(bChars);
		a = AS_STRING(peek(vm, 1));

		concat(vm, a, b);
	}
	

}

static bool call(VM* vm, ObjClosure* closure, size_t argCount) {

	size_t expected = closure->function->arity;

	if (argCount != expected) {
		if (closure->function->lambda) { // From the users pov, lambdas do not arity check
			if (argCount < expected) {
				for (size_t i = argCount; i < expected; i++) {
					push(vm, NULL_VAL);
				}
			}
			else {
				for (size_t i = argCount; i > expected; i--) {
					pop(vm);
				}
			}
		}
		else {
			runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount);
			return false;
		}
	}

	if (vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm->frames[vm->frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;

	frame->slots = vm->stackTop - expected - 1;
	return true;
}

bool callValue(VM* vm, Value callee, size_t argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

			case OBJ_CLASS: {
				ObjClass* klass = AS_CLASS(callee);
				vm->stackTop[-((int)argCount) - 1] = OBJ_VAL(newInstance(vm, klass));

				Value initalizer;
				if (tableGet(&klass->methods, klass->name, &initalizer)) {
					return call(vm, AS_CLOSURE(initalizer), argCount);
				}
				else if (argCount != 0) {
					runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
					return false;
				}

				return true;
			}

			case OBJ_BOUND_METHOD: {
				ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
				vm->stackTop[-((int)argCount) - 1] = bound->receiver;
				return call(vm, bound->method, argCount);
			}

			case OBJ_CLOSURE:
				return call(vm, AS_CLOSURE(callee), argCount);

			case OBJ_NATIVE: {
				ObjNative* obj = AS_NATIVE_OBJ(callee);
				if (argCount != obj->arity) {
					runtimeError(vm, "Expected %d arguments but got %d.", obj->arity, argCount);
					return false;
				}

				NativeFn native = AS_NATIVE(callee);
				bool hasError = false;
				Value result = native(vm, argCount, vm->stackTop - argCount, obj->isBound ? &obj->bound : NULL, &hasError);
				vm->stackTop -= argCount + 1;
				push(vm, result);
				return !hasError;
			}

			default:
				// Non-callable object type.
				break;
		}
	}

	runtimeError(vm, "Can only call functions and classes.");
	return false;
}

static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm->openUpvalues;

	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(vm, local);

	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		vm->openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvalues(VM* vm, Value* last) {
	while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {

		ObjUpvalue* upvalue = vm->openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm->openUpvalues = upvalue->next;

	}
}

static void defineMethod(VM* vm, ObjString* name) {
	Value method = peek(vm, 0);
	ObjClass* klass = AS_CLASS(peek(vm, 1));
	tableSet(vm, &klass->methods, name, method);
	pop(vm);
}

static bool bindMethod(VM* vm, ObjClass* klass, ObjString* name) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod* bound = newBoundMethod(vm, peek(vm, 0), AS_CLOSURE(method));
	pop(vm);
	push(vm, OBJ_VAL(bound));
	return true;
}

static bool invokeFromClass(VM* vm, ObjInstance* instance, ObjClass* klass, ObjString* name, size_t argCount) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}

	if (IS_NATIVE(method)) {
		ObjNative* native = AS_NATIVE_OBJ(method);
		native->isBound = true;
		native->bound = OBJ_VAL(instance);
		return callValue(vm, OBJ_VAL(native), argCount);
	}
	return call(vm, AS_CLOSURE(method), argCount);
}

static bool invoke(VM* vm, ObjString* name, int argCount) {
	Value receiver = peek(vm, argCount);
	if (IS_INSTANCE(receiver)) {
		ObjInstance* instance = AS_INSTANCE(receiver);

		Value value;
		if (tableGet(&instance->fields, name, &value)) {
			vm->stackTop[-argCount - 1] = value;
			return callValue(vm, value, argCount);
		}

		return invokeFromClass(vm, instance, instance->class, name, argCount);
	}
	else if (IS_LIST(receiver)) {
		Value value;
		if (tableGet(&vm->listMethods, name, &value)) {
			vm->stackTop[-argCount - 1] = receiver;
			ObjNative* native = AS_NATIVE_OBJ(value);
			native->isBound = true;
			native->bound = receiver;
			return callValue(vm, OBJ_VAL(native), argCount);
		}

		runtimeError(vm, "Unknown list method.");
		return false;

	}
	else if (IS_STRING(receiver)) {
		Value value;
		if (tableGet(&vm->stringMethods, name, &value)) {
			vm->stackTop[-argCount - 1] = receiver;
			ObjNative* native = AS_NATIVE_OBJ(value);
			native->isBound = true;
			native->bound = receiver;
			return callValue(vm, OBJ_VAL(native), argCount);
		}

		runtimeError(vm, "Unknown string method.");
		return false;
	}
	else {
		runtimeError(vm, "Only instances have methods.");
		return false;
	}
}

InterpreterResult execute(VM* vm, Chunk* chunk) {
	CallFrame* frame = &vm->frames[vm->frameCount - 1];


#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() (AS_STRING(READ_CONSTANT()))

#define BINARY_OP(vm, valueType, op) \
	do { \
	  if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
		runtimeError(vm, "Operands must be numbers."); \
		return STATUS_RUNTIME_ERR; \
	  } \
	  double b = AS_NUMBER(pop(vm)); \
	  double a = AS_NUMBER(pop(vm)); \
	  push(vm, valueType(a op b)); \
	} while (false)

#define BINARY_INTEGER_OP(vm, valueType, op) \
    do { \
	  if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
		runtimeError(vm, "Operands must be numbers."); \
		return STATUS_RUNTIME_ERR; \
	  } \
	  int64_t b = (int64_t) AS_NUMBER(pop(vm)); \
	  int64_t a = (int64_t) AS_NUMBER(pop(vm)); \
	  push(vm, valueType(a op b)); \
	} while (false)

	for (;;) {

#ifdef FOX_DEBUG_STACK_TRACE
		printf("[ ");

		for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
			Value v = *slot;
			char* string = valueToString(v);
			printf("%s ", string);
			free(string);
		}

		printf("]\n");
#endif

#ifdef FOX_DEBUG_EXEC_TRACE
		disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
		printf("\n");
#endif

		uint8_t instruction = READ_BYTE();

		switch (instruction) {

			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				push(vm, constant);
				break;
			}

			case OP_DUP: push(vm, peek(vm, 0)); break;

			case OP_NULL: push(vm, NULL_VAL); break;
			case OP_TRUE: push(vm, BOOL_VAL(true)); break;
			case OP_FALSE: push(vm, BOOL_VAL(false)); break;

			case OP_POP: pop(vm); break;

			case OP_NEGATE: {
				if (!IS_NUMBER(peek(vm, 0))) {
					runtimeError(vm, "Operand must be a number."); //TODO
					return STATUS_RUNTIME_ERR;
				}

				push(vm, NUMBER_VAL(-pop(vm).number));
				break;
			}

			case OP_BITWISE_NOT: {
				if (!IS_NUMBER(peek(vm, 0))) {
					runtimeError(vm, "Operand must be a number."); //TODO
					return STATUS_RUNTIME_ERR;
				}

				int64_t integer = (int64_t)pop(vm).number;

				push(vm, NUMBER_VAL(~integer));

				break;
			}

			case OP_BITWISE_AND: BINARY_INTEGER_OP(vm, NUMBER_VAL, &); break;
			case OP_BITWISE_OR: BINARY_INTEGER_OP(vm, NUMBER_VAL, |); break;
			case OP_XOR: BINARY_INTEGER_OP(vm, NUMBER_VAL, ^); break;
			case OP_LSH: BINARY_INTEGER_OP(vm, NUMBER_VAL, <<); break;
			case OP_RSH: {
				if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {
					runtimeError(vm, "Operands must be numbers.");
					return STATUS_RUNTIME_ERR;
				}
				uint64_t b = (uint64_t)AS_NUMBER(pop(vm));
				uint64_t a = (uint64_t)AS_NUMBER(pop(vm));
				push(vm, NUMBER_VAL(a >> b)); 
				break;
			}
			case OP_ASH: BINARY_INTEGER_OP(vm, NUMBER_VAL, >>); break;

			case OP_EQUAL: {
				Value b = pop(vm);
				Value a = pop(vm);
				push(vm, BOOL_VAL(valuesEqual(a, b)));
				break;
			}

			case OP_GREATER:  BINARY_OP(vm, BOOL_VAL, >); break;
			case OP_LESS:     BINARY_OP(vm, BOOL_VAL, <); break;
			case OP_GREATER_EQ:  BINARY_OP(vm, BOOL_VAL, >=); break;
			case OP_LESS_EQ:     BINARY_OP(vm, BOOL_VAL, <=); break;

			case OP_NOT: {
				push(vm, BOOL_VAL(isFalsey(pop(vm))));
				break;
			}

			case OP_ADD: {
				if (IS_LIST(peek(vm, 1))) {
					Value toAppend = pop(vm);
					ObjList* list = AS_LIST(pop(vm));

					ValueArray array;
					initValueArray(&array);
					for (size_t i = 0; i < list->items.count; i++) {
						writeValueArray(vm, &array, list->items.values[i]);
					}
					writeValueArray(vm, &array, toAppend);

					ObjList* nList = newList(vm, array);

					push(vm, OBJ_VAL(nList));
					break;
				}
				else if (IS_STRING(peek(vm, 0)) || IS_STRING(peek(vm, 1))) {
					concatenate(vm, IS_STRING(peek(vm, 0)), IS_STRING(peek(vm, 1)));
					break;
				}

				BINARY_OP(vm, NUMBER_VAL, +);
				break;
			}

			case OP_SUB: {
				BINARY_OP(vm, NUMBER_VAL, -);
				break;
			}

			case OP_DIV: {
				BINARY_OP(vm, NUMBER_VAL, /);
				break;
			}

			case OP_MUL: {
				BINARY_OP(vm, NUMBER_VAL, *);
				break;
			}

			case OP_DEFINE_GLOBAL: {
				ObjString* name = READ_STRING();
				tableSet(vm, &vm->globals, name, peek(vm, 0));
				pop(vm);
				break;
			}

			case OP_SET_GLOBAL: {
				ObjString* name = READ_STRING();
				if (tableSet(vm, &vm->globals, name, peek(vm, 0))) {
					tableDelete(&vm->globals, name);
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return STATUS_RUNTIME_ERR;
				}
				break;
			}

			case OP_GET_GLOBAL: {
				ObjString* name = READ_STRING();
				Value value;
				if (!tableGet(&vm->globals, name, &value)) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return STATUS_RUNTIME_ERR;
				}
				push(vm, value);
				break;
			}

			case OP_GET_LOCAL: {
				uint8_t slot = READ_BYTE();
				push(vm, frame->slots[slot]);
				break;
			}

			case OP_SET_LOCAL: {
				uint8_t slot = READ_BYTE();
				frame->slots[slot] = peek(vm, 0);
				break;
			}

			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(pop(vm))) frame->ip += offset;
				break;
			}

			case OP_JUMP_IF_FALSE_S: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(vm, 0))) frame->ip += offset;
				break;
			}

			case OP_JUMP: {
				uint16_t offset = READ_SHORT();
				frame->ip += offset;
				break;
			}

			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
				frame->ip -= offset;
				break;
			}

			case OP_CALL: {
				int argCount = READ_BYTE();
				if (!callValue(vm, peek(vm, argCount), argCount)) {
					return STATUS_RUNTIME_ERR;
				}
				frame = &vm->frames[vm->frameCount - 1];
				break;
			}

			case OP_CLOSURE: {
				ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
				ObjClosure* closure = newClosure(vm, function);
				push(vm, OBJ_VAL(closure));
				for (size_t i = 0; i < closure->upvalueCount; i++) {
					uint8_t isLocal = READ_BYTE();
					uint8_t index = READ_BYTE();
					if (isLocal) {
						closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
					}
					else {
						closure->upvalues[i] = frame->closure->upvalues[index];
					}
				}
				break;
			}

			case OP_INVOKE: {
				ObjString* method = READ_STRING();
				size_t argCount = READ_BYTE();
				if (!invoke(vm, method, argCount)) {
					return STATUS_RUNTIME_ERR;
				}
				frame = &vm->frames[vm->frameCount - 1];
				break;
			}

			case OP_GET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				push(vm, *frame->closure->upvalues[slot]->location);
				break;
			}

			case OP_SET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				*frame->closure->upvalues[slot]->location = peek(vm, 0);
				break;
			}

			case OP_CLOSE_UPVALUE: {
				closeUpvalues(vm, vm->stackTop - 1);
				pop(vm);
				break;
			}

			case OP_CLASS:
				push(vm, OBJ_VAL(newClass(vm, READ_STRING())));
				break;

			case OP_INHERIT: {
				Value superclass = peek(vm, 1);
				if (!IS_CLASS(superclass)) {
					runtimeError(vm, "Superclass must be a class.");
					return STATUS_RUNTIME_ERR;
				}

				ObjClass* subclass = AS_CLASS(peek(vm, 0));
				tableAddAll(vm, &AS_CLASS(superclass)->methods, &subclass->methods);
				pop(vm); // Subclass.
				break;
			}

			case OP_GET_SUPER: {
				ObjString* name = READ_STRING();
				ObjClass* superclass = AS_CLASS(pop(vm));
				if (!bindMethod(vm, superclass, name)) {
					return STATUS_RUNTIME_ERR;
				}
				break;
			}

			case OP_SUPER_INVOKE: {
				ObjString* method = READ_STRING();
				size_t argCount = READ_BYTE();
				ObjClass* superclass = AS_CLASS(pop(vm));
				if (!invokeFromClass(vm, AS_INSTANCE(frame->slots[0]), superclass, method, argCount)) {
					return STATUS_RUNTIME_ERR;
				}
				frame = &vm->frames[vm->frameCount - 1];
				break;
			}

			case OP_METHOD:
				defineMethod(vm, READ_STRING());
				break;

			case OP_OBJECT: {
				push(vm, OBJ_VAL(vm->objectClass));
				break;
			}

			case OP_GET_PROPERTY: {

				ObjString* name = READ_STRING();

				if (IS_INSTANCE(peek(vm, 0))) {
					ObjInstance* instance = AS_INSTANCE(peek(vm, 0));

					Value value;
					if (tableGet(&instance->fields, name, &value)) {
						pop(vm); // Instance.
						push(vm, value);
						break;
					}
					if (!bindMethod(vm, instance->class, name)) {
						return STATUS_RUNTIME_ERR;
					}
					break;
				}

				else if (IS_LIST(peek(vm, 0))) {
					Value value;
					tableGet(&vm->listMethods, name, &value);
					ObjNative* native = AS_NATIVE_OBJ(value);
					native->bound = peek(vm, 0);
					native->isBound = true;
					pop(vm);
					push(vm, OBJ_VAL(native));
					break;
				}
				else if (IS_STRING(peek(vm, 0))) {
					Value value;
					tableGet(&vm->stringMethods, name, &value);
					ObjNative* native = AS_NATIVE_OBJ(value);
					native->bound = peek(vm, 0);
					native->isBound = true;
					pop(vm);
					push(vm, OBJ_VAL(native));
					break;
				}

				else {
					runtimeError(vm, "Only instances can contain properties.");
					return STATUS_RUNTIME_ERR;
				}
			}

			case OP_SET_PROPERTY: {
				if (!IS_INSTANCE(peek(vm, 1))) {
					runtimeError(vm, "Only instances can contain properties.");
					return STATUS_RUNTIME_ERR;
				}

				ObjInstance* instance = AS_INSTANCE(peek(vm, 1));
				tableSet(vm, &instance->fields, READ_STRING(), peek(vm, 0));

				Value value = pop(vm);
				pop(vm);
				push(vm, value);
				break;
			}

			case OP_LIST: {
				uint8_t itemCount = READ_BYTE();

				ValueArray items;
				initValueArray(&items);
				for (size_t i = 0; i < itemCount; i++) {
					writeValueArray(vm, &items, peek(vm, itemCount - i - 1));
				}
				for (size_t i = 0; i < itemCount; i++) {
					pop(vm);
				}

				ObjList* list = newList(vm, items);
				push(vm, OBJ_VAL(list));

				break;
			}

			case OP_GET_INDEX: {
				if (IS_INSTANCE(peek(vm, 1))) {

					ObjInstance* instance = AS_INSTANCE(peek(vm, 1));

					if (!IS_STRING(peek(vm, 0))) {
						runtimeError(vm, "Can only index instances using strings.");
						return STATUS_RUNTIME_ERR;
					}

					ObjString* name = AS_STRING(pop(vm));

					Value value;
					if (tableGet(&instance->fields, name, &value)) {
						pop(vm); // Instance.
						push(vm, value);
						break;
					}
					if (!bindMethod(vm, instance->class, name)) {
						return STATUS_RUNTIME_ERR;
					}
					break;

				}

				if (IS_STRING(peek(vm, 1))) {
					ObjString* string = AS_STRING(peek(vm, 1));

					if (!IS_NUMBER(peek(vm, 0)) || ceil(AS_NUMBER(peek(vm, 0))) != AS_NUMBER(peek(vm, 0))) {
						runtimeError(vm, "Can only index strings using integers.");
						return STATUS_RUNTIME_ERR;
					}

					double dindex = AS_NUMBER(peek(vm, 0));
					size_t index = (size_t)dindex;

					if (index >= string->length) {
						runtimeError(vm, "Index out of bounds.");
						return STATUS_RUNTIME_ERR;
					}

					Value v = OBJ_VAL(copyString(vm, &string->chars[index], 1));
					pop(vm);
					pop(vm);
					push(vm, v);
					break;
				}

				if (!IS_LIST(peek(vm, 1))) {
					runtimeError(vm, "Can only index into lists.");
					return STATUS_RUNTIME_ERR;
				}

				ObjList* list = AS_LIST(peek(vm, 1));

				if (!IS_NUMBER(peek(vm, 0)) || ceil(AS_NUMBER(peek(vm, 0))) != AS_NUMBER(peek(vm, 0))) {
					runtimeError(vm, "Can only index lists using integers.");
					return STATUS_RUNTIME_ERR;
				}

				double dindex = AS_NUMBER(peek(vm, 0));
				size_t index = (size_t)dindex;

				if (index >= list->items.count) {
					runtimeError(vm, "Index out of bounds.");
					return STATUS_RUNTIME_ERR;
				}

				Value v = list->items.values[index];
				pop(vm);
				pop(vm);
				push(vm, v);
				break;
			}

			case OP_SET_INDEX: {

				if (IS_INSTANCE(peek(vm, 2))) {

					ObjInstance* instance = AS_INSTANCE(peek(vm, 2));

					if (!IS_STRING(peek(vm, 1))) {
						runtimeError(vm, "Can only index instances using strings.");
						return STATUS_RUNTIME_ERR;
					}

					ObjString* name = AS_STRING(peek(vm, 1));

					Value value = peek(vm, 0);
					tableSet(vm, &instance->fields, name, value);
					value = pop(vm);
					pop(vm);
					pop(vm);
					push(vm, value);
					break;

				}


				if (!IS_LIST(peek(vm, 2))) {
					runtimeError(vm, "Can only index into lists.");
					return STATUS_RUNTIME_ERR;
				}

				ObjList* list = AS_LIST(peek(vm, 2));

				if (!IS_NUMBER(peek(vm, 1)) || ceil(AS_NUMBER(peek(vm, 1))) != AS_NUMBER(peek(vm, 1))) {
					runtimeError(vm, "Can only index lists using integers.");
					return STATUS_RUNTIME_ERR;
				}

				double dindex = AS_NUMBER(peek(vm, 1));
				size_t index = (size_t)dindex;

				if (index >= list->items.count) {
					runtimeError(vm, "Index out of bounds.");
					return STATUS_RUNTIME_ERR;
				}

				Value v = pop(vm);
				list->items.values[index] = v;
				pop(vm);
				pop(vm);
				push(vm, v);

				break;
			}

			case OP_EXPORT: {
				Value toExport = pop(vm);
				ObjString* string = READ_STRING();

				tableSet(vm, &vm->exports, string, toExport);
				break;
			}

			case OP_IMPORT: {
				ObjString* path = READ_STRING();
				ObjString* name = READ_STRING();

				char* extension = ".fox";

				char* string = malloc(path->length + vm->filepath->length + 4 /*.fox*/ + 1);

				memcpy(string, vm->filepath->chars, vm->filepath->length);
				memcpy(string + vm->filepath->length, path->chars, path->length);
				memcpy(string + vm->filepath->length + path->length, extension, 4);
				string[path->length + vm->filepath->length + 4] = '\0';

				if (_access(string, 0) == 0) {
					Value object;
					import(vm, string, name, &object);
					push(vm, object);
					free(string);
				}
				else {
					string = malloc(path->length + vm->basePath->length + 4 /*.fox*/ + 1);

					memcpy(string, vm->basePath->chars, vm->basePath->length);
					memcpy(string + vm->basePath->length, path->chars, path->length);
					memcpy(string + vm->basePath->length + path->length, extension, 4);
					string[path->length + vm->basePath->length + 4] = '\0';

					if (_access(string, 0) == 0) {
						Value object;
						import(vm, string, name, &object);
						push(vm, object);
						free(string);
					}

					else {
						runtimeError(vm, "Could not find import '%s'", path->chars);
						free(string);
						return STATUS_RUNTIME_ERR;
					}
				}

				break;
			}

			case OP_RETURN: {
				Value result = pop(vm);

				closeUpvalues(vm, frame->slots);

				vm->frameCount--;
				if (vm->frameCount == 0) {
					pop(vm);
					return STATUS_OK;
				}

				vm->stackTop = frame->slots;
				push(vm, result);

				frame = &vm->frames[vm->frameCount - 1];
				break;
			}
		}

	}

	return STATUS_OK;
#undef READ_STRING
#undef READ_CONSTANT
#undef READ_SHORT
}

InterpreterResult interpret(char* basePath, char* filename, const char* source) {
	VM vm;
	initVM(&vm);

	InterpreterResult result = interpretVM(&vm, basePath, filename, source);

	freeVM(&vm);

	return result;
}

InterpreterResult interpretVM(VM* vm, char* basePath, char* filename, const char* source) {

	Chunk chunk;
	initChunk(&chunk);

	ObjString* base = copyString(vm, basePath, strlen(basePath));

	vm->basePath = base;
	vm->filepath = base;

	vm->filename = filename;

	ObjFunction* function = compile(vm, source, &chunk);
	if (function == NULL) return STATUS_COMPILE_ERR;

	vm->compiler = NULL;

	push(vm, OBJ_VAL(function));

	ObjClosure* closure = newClosure(vm, function);
	pop(vm);
	push(vm, OBJ_VAL(closure));
	callValue(vm, OBJ_VAL(closure), 0);

	return execute(vm, &vm->frames[vm->frameCount - 1].closure->function->chunk);
}

InterpreterResult import(VM* importingVm, char* path, ObjString* name, Value* value) {
	VM* vm = malloc(sizeof(VM));
	initVM(vm);

	vm->isImport = true;

	Chunk chunk;
	initChunk(&chunk);

	vm->basePath = importingVm->basePath;
	vm->filename = malloc(name->length + 5);
	strcpy(vm->filename, name->chars);
	strcpy(vm->filename + name->length, ".fox");

	int filepathIndex = (fromLastInstance(path, "/") - path) + 1;

	char* filepath = malloc(filepathIndex + 1);
	memcpy(filepath, path, filepathIndex);
	filepath[filepathIndex] = '\0';

	vm->filepath = takeString(vm, filepath, filepathIndex);

	char* source = readFile(path);
	if (source == NULL) { 
		fprintf(stderr, "\n");
		return STATUS_RUNTIME_ERR; 
	}

	ObjFunction* function = compile(vm, source, &chunk);
	if (function == NULL) return STATUS_COMPILE_ERR;

	vm->compiler = NULL;

	push(vm, OBJ_VAL(function));

	ObjClosure* closure = newClosure(vm, function);
	pop(vm);
	push(vm, OBJ_VAL(closure));
	callValue(vm, OBJ_VAL(closure), 0);

	InterpreterResult result = execute(vm, &vm->frames[vm->frameCount - 1].closure->function->chunk);

	ObjInstance* obj = newInstance(importingVm, importingVm->importClass);

	for (int i = 0; i <= vm->exports.capacity; i++) {
		Entry* entry = &vm->exports.entries[i];
		if (entry->key != NULL) {
			// Copying the string due to interning making them different
			tableSet(importingVm, &obj->fields, copyString(importingVm, entry->key->chars, entry->key->length), entry->value);
		}
	}

	*value = OBJ_VAL(obj);

	// Cleans up memory which can no longer be accessed.
	collectGarbage(vm);

	if (importingVm->importCapacity < importingVm->importCount + 1) {
		size_t oldCapacity = importingVm->importCapacity;
		importingVm->importCapacity = importingVm->importCapacity < 8 ? 8 : importingVm->importCapacity * 2;
		importingVm->imports = GROW_ARRAY(importingVm, VM*, importingVm->imports, oldCapacity, importingVm->importCapacity);
	}

	importingVm->imports[importingVm->importCount++] = vm;

	return result;
}

void freeVM(VM* vm) {

	// Frees the VMs of the imports, as they are not freed in the import function
	for (size_t i = 0; i < vm->importCount; i++) {
		freeVM(vm->imports[i]);
	}

	freeTable(vm, &vm->strings);
	freeTable(vm, &vm->globals);
	freeObjects(vm);
	free(vm->filename);
	free(vm->grayStack);
	free(vm->imports);
	if (vm->isImport) free(vm);
}