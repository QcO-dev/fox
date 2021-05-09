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
#include <natives/iterator.h>
#include <natives/exception.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <io.h>

static InterpreterResult import(VM* vm, char* path, ObjString* name, Value* value);

void initVM(VM* vm, char* name) {

	vm->frameSize = 64;
	vm->frames = malloc(vm->frameSize * sizeof(CallFrame));

	vm->stackSize = 256 * vm->frameSize;

	vm->stack = malloc(vm->stackSize * sizeof(Value));

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

	ObjClass* iteratorClass = newClass(vm, copyString(vm, "Iterator", 8));
	vm->iteratorClass = iteratorClass;
	defineIteratorMethods(vm, vm->iteratorClass);
	defineObjectMethods(vm, vm->iteratorClass);

	ObjClass* exceptionClass = newClass(vm, copyString(vm, "Exception", 9));
	vm->exceptionClass = exceptionClass;
	defineExceptionMethods(vm, vm->exceptionClass);
	defineObjectMethods(vm, vm->exceptionClass);

	tableSet(vm, &vm->globals, copyString(vm, "Object", 6), OBJ_VAL(vm->objectClass));
	tableSet(vm, &vm->globals, copyString(vm, "<object>", 8), OBJ_VAL(vm->objectClass)); // Allows super() in classes with no superclass
	tableSet(vm, &vm->globals, copyString(vm, "Iterator", 8), OBJ_VAL(vm->iteratorClass));
	tableSet(vm, &vm->globals, copyString(vm, "Exception", 9), OBJ_VAL(vm->exceptionClass));

	tableSet(vm, &vm->globals, copyString(vm, "_NAME", 5), OBJ_VAL(copyString(vm, name, strlen(name))));

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

	size_t prevLine = 0;
	ObjFunction* prevFunction = NULL;
	int count = 0;
	bool repeating = false;

	for (int i = vm->frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm->frames[i];
		ObjFunction* function = frame->closure->function;
		// -1 because the IP is sitting on the next instruction to be executed.
		size_t instruction = frame->ip - function->chunk.code - 1;

		size_t line = getLine(&function->chunk.table, instruction);

		if (line != prevLine || function != prevFunction) {
			if (repeating == true) {
				fprintf(stderr, "[Previous * %d]\n", count);
				repeating = false;
				count = 0;
			}
			fprintf(stderr, "[%d] in ", line);
			if (function->name == NULL) {
				fprintf(stderr, "<script>\n");
			}
			else {
				fprintf(stderr, "%s\n", function->name->chars);
			}
			prevFunction = function;
			prevLine = line;
		}
		else {
			repeating = true;
			count++;
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

		char* aChars = valueToString(vm, aValue);
		a = copyString(vm, aChars, strlen(aChars));
		free(aChars);
		concat(vm, a, b);
	}
	else {
		Value bValue = peek(vm, 0);
		char* bChars = valueToString(vm, bValue);
		b = copyString(vm, bChars, strlen(bChars));
		free(bChars);
		a = AS_STRING(peek(vm, 1));

		concat(vm, a, b);
	}
	

}

static bool call(VM* vm, ObjClosure* closure, size_t argCount) {

	size_t expected = closure->function->arity;

	if (closure->function->varArgs) {
		size_t needed = expected - 1; // The arity minus the variable arguments.
		if (argCount < needed) {
			if (closure->function->lambda) {
				for (size_t i = argCount; i < needed; i++) {
					push(vm, NULL_VAL);
				}
			}
			else {
				pop(vm);
				return throwException(vm, "ArityException", "Expected %d or more arguments but got %d.", needed, argCount);
			}
		}

		size_t varArgCount = argCount - needed;

		ValueArray varArgs;
		initValueArray(&varArgs);

		// 2 loops are used to prevent the GC from cleaning items which are still needed.
		if (needed == 0) {
			for (size_t i = varArgCount; i > needed; i--) {
				writeValueArray(vm, &varArgs, peek(vm, i - 1));
			}
			for (size_t i = varArgCount; i > needed; i--) {
				pop(vm);
			}
		}
		else {
			for (size_t i = varArgCount; i >= needed; i--) {
				writeValueArray(vm, &varArgs, peek(vm, i - 1));
			}
			for (size_t i = varArgCount; i >= needed; i--) {
				pop(vm);
			}
		}

		ObjList* list = newList(vm, varArgs);

		push(vm, OBJ_VAL(list));

	}
	else {
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
				pop(vm);
				return throwException(vm, "ArityException", "Expected %d arguments but got %d.", closure->function->arity, argCount);
			}
		}
	}

	if (vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "StackOverflowException: Stack limit reached (%d frames)", FRAMES_MAX);
		return false;
	}

	if (vm->frameCount + 1 == vm->frameSize) {
		int oldCount = vm->frameSize;
		vm->frameSize = vm->frameSize < 8 ? 8 : vm->frameSize * 2;
		vm->frames = GROW_ARRAY(vm, CallFrame, vm->frames, oldCount, vm->frameSize);

		size_t oldStackSize = vm->stackSize;
		size_t stackHeadDistance = vm->stackTop - vm->stack;

		vm->stackSize = 256 * vm->frameSize;

		vm->stack = GROW_ARRAY(vm, Value, vm->stack, oldStackSize, vm->stackSize);
		vm->stackTop = &vm->stack[stackHeadDistance];
	}

	CallFrame* frame = &vm->frames[vm->frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->isTry = false;

	frame->slots = vm->stackTop - expected - 1;
	vm->frame = frame;
	return true;
}

bool callValue(VM* vm, Value callee, size_t argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

			case OBJ_CLASS: {
				ObjClass* klass = AS_CLASS(callee);
				Value instance = OBJ_VAL(newInstance(vm, klass));
				vm->stackTop[-((int)argCount) - 1] = instance;

				Value initalizer;
				if (tableGet(&klass->methods, klass->name, &initalizer)) {
					// Handle classes with initalizers which are native (such as <iterator>)
					if (IS_NATIVE(initalizer)) {
						ObjNative* native = AS_NATIVE_OBJ(initalizer);
						native->isBound = true;
						native->bound = instance;
						return callValue(vm, OBJ_VAL(native), argCount);
					}

					return call(vm, AS_CLOSURE(initalizer), argCount);
				}
				else if (argCount != 0) {
					pop(vm);
					pop(vm);
					return throwException(vm, "ArityException", "Expected 0 arguments but got %d.", argCount);
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
					if (!(obj->varArgs && argCount > obj->arity)) {
						pop(vm);
						return throwException(vm, "ArityException", "Expected %d arguments but got %d.", obj->arity, argCount);
					}
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
	pop(vm);
	return throwException(vm, "InvalidOperationException", "Can only call functions and classes.");
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
		pop(vm);
		pop(vm);
		return throwException(vm, "UndefinedPropertyException", "Undefined property '%s'.", name->chars);
	}

	if (IS_NATIVE(method)) {
		ObjNative* native = AS_NATIVE_OBJ(method);
		native->isBound = true;
		native->bound = OBJ_VAL(instance);
		return callValue(vm, OBJ_VAL(native), argCount);
	}
	return call(vm, AS_CLOSURE(method), argCount);
}

static bool throwGeneral(VM* vm, ObjInstance* throwee) {
	Table* fields = &throwee->fields;
	push(vm, OBJ_VAL(throwee));
	tableSet(vm, fields, copyString(vm, "filename", 8), OBJ_VAL(copyString(vm, vm->filename, strlen(vm->filename))));

	ObjFunction* function = vm->frame->closure->function;

	size_t instruction = vm->frame->ip - function->chunk.code - 1;

	size_t line = getLine(&function->chunk.table, instruction);

	tableSet(vm, fields, copyString(vm, "line", 4), NUMBER_VAL(line));

	ValueArray stackTrace;
	initValueArray(&stackTrace);

	while (!vm->frame->isTry) {
		Value result = pop(vm);

		closeUpvalues(vm, vm->frame->slots);

		function = vm->frame->closure->function;

		instruction = vm->frame->ip - function->chunk.code - 1;

		line = getLine(&function->chunk.table, instruction);

		// In form:
		// [%d] in %s <- line, function name
		int lineNumberLength = snprintf(NULL, 0, "%d", line);
		size_t lineLength = 1 + lineNumberLength + 1 + 4 + (function->name == NULL ? 8 : function->name->length);
		char* str = malloc(lineLength + 1);
		sprintf(str, "[%d] in %s", line, function->name == NULL ? "<script>" : function->name->chars);

		writeValueArray(vm, &stackTrace, OBJ_VAL(takeString(vm, str, lineLength)));

		vm->frameCount--;
		if (vm->frameCount == 0) {
			pop(vm);

			Value value;
			char* valueString = "\0";
			if (tableGet(fields, copyString(vm, "value", 5), &value)) {
				valueString = valueToString(vm, value);
			}

			Value name;
			char* nameString = NULL;
			if (tableGet(fields, copyString(vm, "name", 4), &name)) {
				nameString = valueToString(vm, name);
			}

			fprintf(stderr, "%s: %s\nIn file %s:\n", nameString == NULL ? "Exception" : nameString, valueString, vm->filename);

			for (size_t i = 0; i < stackTrace.count; i++) {
				fprintf(stderr, "%s\n", AS_CSTRING(stackTrace.values[i]));
			}

			return false;
		}

		vm->stackTop = vm->frame->slots;
		push(vm, result);

		vm->frame = &vm->frames[vm->frameCount - 1];
	}

	// Append last call (the one inside the try block)
	function = vm->frame->closure->function;

	instruction = vm->frame->ip - function->chunk.code - 1;

	line = getLine(&function->chunk.table, instruction);

	// In form:
	// [%d] in %s <- line, function name
	int lineNumberLength = snprintf(NULL, 0, "%d", line);
	size_t lineLength = 1 + lineNumberLength + 1 + 4 + (function->name == NULL ? 8 : function->name->length);
	char* str = malloc(lineLength + 1);
	sprintf(str, "[%d] in %s", line, function->name == NULL ? "<script>" : function->name->chars);

	writeValueArray(vm, &stackTrace, OBJ_VAL(takeString(vm, str, lineLength)));


	ObjList* stackTraceList = newList(vm, stackTrace);

	tableSet(vm, fields, copyString(vm, "stack", 5), OBJ_VAL(stackTraceList));

	vm->frame->isTry = false;
	vm->frame->ip = vm->frame->catchJump;

	return true;
}

bool throwException(VM* vm, char* name, char* reason, ...) {

	va_list args;
	va_start(args, reason);
	int length = vsnprintf(NULL, 0, reason, args);
	char* value = malloc(length + 1);
	vsprintf(value, reason, args);
	va_end(args);

	ObjInstance* inst = newInstance(vm, vm->exceptionClass);

	Table* fields = &inst->fields;

	tableSet(vm, fields, copyString(vm, "value", 5), OBJ_VAL(takeString(vm, value, strlen(value))));
	tableSet(vm, fields, copyString(vm, "name", 4), OBJ_VAL(copyString(vm, name, strlen(name))));

	return throwGeneral(vm, inst);
}

bool invoke(VM* vm, ObjString* name, int argCount) {
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
		pop(vm);
		pop(vm);
		return throwException(vm, "UndefinedPropertyException", "Undefined list method.");
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

		pop(vm);
		pop(vm);
		return throwException(vm, "UndefinedPropertyException", "Undefined string method.");
	}
	else {
		pop(vm);
		pop(vm);
		return throwException(vm, "InvalidOperationException", "Only instances have properties.");
	}
}

InterpreterResult execute(VM* vm, Chunk* chunk) {
	vm->frame = &vm->frames[vm->frameCount - 1];


#define READ_BYTE() (*vm->frame->ip++)
#define READ_SHORT() (vm->frame->ip += 2, (uint16_t)((vm->frame->ip[-2] << 8) | vm->frame->ip[-1]))
#define READ_CONSTANT() (vm->frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() (AS_STRING(READ_CONSTANT()))
	
#define BINARY_OP(vm, valueType, op) \
	do { \
		if (IS_INSTANCE(peek(vm, 1))) { \
			if (!invoke(vm, copyString(vm, #op, strlen(#op)), 1)) { \
				return STATUS_RUNTIME_ERR; \
			} \
			vm->frame = &vm->frames[vm->frameCount - 1]; \
			break; \
	   } \
	  if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
		pop(vm); \
		pop(vm); \
		if(!throwException(vm, "InvalidOperationException", "Operands must be numbers.")) return STATUS_RUNTIME_ERR;\
		break;\
	  } \
	  double b = AS_NUMBER(pop(vm)); \
	  double a = AS_NUMBER(pop(vm)); \
	  push(vm, valueType(a op b)); \
	} while (false)

#define BINARY_INTEGER_OP(vm, valueType, op) \
	do { \
	   if (IS_INSTANCE(peek(vm, 1))) { \
			if (!invoke(vm, copyString(vm, #op, strlen(#op)), 1)) { \
				return STATUS_RUNTIME_ERR; \
			} \
			vm->frame = &vm->frames[vm->frameCount - 1]; \
			break; \
	   } \
	  if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
		pop(vm); \
		pop(vm); \
		if(!throwException(vm, "InvalidOperationException", "Operands must be numbers.")) return STATUS_RUNTIME_ERR;\
		break;\
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
			char* string = valueToString(vm, v);
			printf("%s ", string);
			free(string);
		}

		printf("]\n");
#endif

#ifdef FOX_DEBUG_EXEC_TRACE
		disassembleInstruction(vm, &vm->frame->closure->function->chunk, (int)(vm->frame->ip - vm->frame->closure->function->chunk.code));
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

			case OP_DUP_OFFSET: {
				uint8_t offset = READ_BYTE();
				push(vm, peek(vm, offset)); 
				break; 
			}

			case OP_SWAP: {
				Value a = pop(vm);
				Value b = pop(vm);
				push(vm, a);
				push(vm, b);
				break;
			}

			case OP_SWAP_OFFSET: {
				uint8_t offset = READ_BYTE();

				Value a = peek(vm, offset);
				Value b = pop(vm);

				vm->stackTop[-offset]= b;
				push(vm, a);
				break;
			}

			case OP_NULL: push(vm, NULL_VAL); break;
			case OP_TRUE: push(vm, BOOL_VAL(true)); break;
			case OP_FALSE: push(vm, BOOL_VAL(false)); break;

			case OP_POP: pop(vm); break;

			case OP_NEGATE: {

				if (IS_INSTANCE(peek(vm, 0))) {
					if (!invoke(vm, copyString(vm, "-", 1), 0)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}

				if (!IS_NUMBER(peek(vm, 0))) {
					pop(vm);
					if (!throwException(vm, "InvalidOperationException", "Operand must be a number.")) return STATUS_RUNTIME_ERR;
					break;
				}

				push(vm, NUMBER_VAL(-pop(vm).number));
				break;
			}

			case OP_BITWISE_NOT: {

				if (IS_INSTANCE(peek(vm, 0))) {
					if (!invoke(vm, copyString(vm, "~", 1), 0)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}

				if (!IS_NUMBER(peek(vm, 0))) {
					pop(vm);
					if (!throwException(vm, "InvalidOperationException", "Operand must be a number.")) return STATUS_RUNTIME_ERR;
					break;
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
				if (IS_INSTANCE(peek(vm, 1))) {
					if (!invoke(vm, copyString(vm, ">>", 2), 1)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}
				if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {
					pop(vm);
					pop(vm);
					if (!throwException(vm, "InvalidOperationException", "Operands must be a numbers.")) return STATUS_RUNTIME_ERR;
					break;
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
				if (IS_INSTANCE(a)) {
					push(vm, a);
					push(vm, b);
					if (!invoke(vm, copyString(vm, "==", 2), 1)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}
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

			case OP_IS: {
				Value b = pop(vm);
				Value a = pop(vm);

				if (a.type == VAL_OBJ && b.type == VAL_OBJ) {
					push(vm, BOOL_VAL(a.obj == b.obj));
				}
				else {
					push(vm, BOOL_VAL(valuesEqual(a, b)));
				}

				break;
			}

			case OP_IN: {
				Value b = pop(vm);
				Value a = pop(vm);

				if (IS_LIST(b)) {
					ObjList* list = AS_LIST(b);
					for (size_t i = 0; i < list->items.count; i++) {
						if (valuesEqual(a, list->items.values[i])) {
							push(vm, BOOL_VAL(true));
							goto exitLoop;
						}
					}
					push(vm, BOOL_VAL(false));
				exitLoop:;
				}
				else if (IS_STRING(b)) {
					if (!IS_STRING(a)) {
						if (!throwException(vm, "InvalidOperationException", "Can only test for strings within strings.")) return STATUS_RUNTIME_ERR;
						break;
					}
					char* string = AS_CSTRING(b);
					char* needle = AS_CSTRING(a);
					push(vm, BOOL_VAL(strstr(string, needle) != NULL));
				}
				else {
					if (!throwException(vm, "InvalidOperationException", "Right hand operator must be iterable.")) return STATUS_RUNTIME_ERR;
					break;
				}
			
				break;
			}

			case OP_RANGE: {
				Value b = pop(vm);
				Value a = pop(vm);

				if (IS_NUMBER(a) && IS_NUMBER(b)) {
					double da = AS_NUMBER(a);
					double db = AS_NUMBER(b);
					if (ceil(da) != da || ceil(db) != db) {
						if (!throwException(vm, "InvalidOperationException", "Operands must be integers.")) return STATUS_RUNTIME_ERR;
						break;
					}
					int64_t ia = (int64_t)da;
					int64_t ib = (int64_t)db;

					ValueArray array;
					initValueArray(&array);

					if (ib > ia) {

						for (int64_t i = ia; i < ib; i++) {
							writeValueArray(vm, &array, NUMBER_VAL(i));
						}

					}
					else {
						for (int64_t i = ia; i > ib; i--) {
							writeValueArray(vm, &array, NUMBER_VAL(i));
						}
					}
					push(vm, OBJ_VAL(newList(vm, array)));
				}
				else {
					if (!throwException(vm, "InvalidOperationException", "Operands must be numbers.")) return STATUS_RUNTIME_ERR;
				}

				break;
			}

			case OP_INCREMENT: {

				if (IS_INSTANCE(peek(vm, 0))) {
					if (!invoke(vm, copyString(vm, "++", 2), 0)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}

				if (!IS_NUMBER(peek(vm, 0))) {
					pop(vm);
					if (!throwException(vm, "InvalidOperationException", "Operand must be a number.")) return STATUS_RUNTIME_ERR;
					break;
				}

				push(vm, NUMBER_VAL(AS_NUMBER(pop(vm)) + 1));

				break;
			}

			case OP_DECREMENT: {

				if (IS_INSTANCE(peek(vm, 0))) {
					if (!invoke(vm, copyString(vm, "--", 2), 0)) {
						return STATUS_RUNTIME_ERR;
					}
					vm->frame = &vm->frames[vm->frameCount - 1];
					break;
				}

				if (!IS_NUMBER(peek(vm, 0))) {
					pop(vm);
					if (!throwException(vm, "InvalidOperationException", "Operand must be a number.")) return STATUS_RUNTIME_ERR;
					break;
				}

				push(vm, NUMBER_VAL(AS_NUMBER(pop(vm)) - 1));

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
					pop(vm);
					if (!throwException(vm, "UndefinedVariableException", "Undefined variable '%s'.", name->chars)) return STATUS_RUNTIME_ERR;
					break;
				}
				break;
			}

			case OP_GET_GLOBAL: {
				ObjString* name = READ_STRING();
				Value value;
				if (!tableGet(&vm->globals, name, &value)) {
					if (!throwException(vm, "UndefinedVariableException", "Undefined variable '%s'.", name->chars)) return STATUS_RUNTIME_ERR;
					break;
				}
				push(vm, value);
				break;
			}

			case OP_GET_LOCAL: {
				uint8_t slot = READ_BYTE();
				push(vm, vm->frame->slots[slot]);
				break;
			}

			case OP_SET_LOCAL: {
				uint8_t slot = READ_BYTE();
				vm->frame->slots[slot] = peek(vm, 0);
				break;
			}

			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(pop(vm))) vm->frame->ip += offset;
				break;
			}

			case OP_JUMP_IF_FALSE_S: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(vm, 0))) vm->frame->ip += offset;
				break;
			}

			case OP_JUMP: {
				uint16_t offset = READ_SHORT();
				vm->frame->ip += offset;
				break;
			}

			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
				vm->frame->ip -= offset;
				break;
			}

			case OP_CALL: {
				int argCount = READ_BYTE();
				if (!callValue(vm, peek(vm, argCount), argCount)) {
					return STATUS_RUNTIME_ERR;
				}
				vm->frame = &vm->frames[vm->frameCount - 1];

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
						closure->upvalues[i] = captureUpvalue(vm, vm->frame->slots + index);
					}
					else {
						closure->upvalues[i] = vm->frame->closure->upvalues[index];
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
				vm->frame = &vm->frames[vm->frameCount - 1];

				break;
			}

			case OP_GET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				push(vm, *vm->frame->closure->upvalues[slot]->location);
				break;
			}

			case OP_SET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				*vm->frame->closure->upvalues[slot]->location = peek(vm, 0);
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
					if (!throwException(vm, "InvalidInheritanceException", "Superclass must be a class.")) return STATUS_RUNTIME_ERR;
					break;
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
					pop(vm);
					if (!throwException(vm, "UndefinedPropertyException", "Undefined Property '%s'", name->chars)) return STATUS_RUNTIME_ERR;
				}
				break;
			}

			case OP_SUPER_INVOKE: {
				ObjString* method = READ_STRING();
				size_t argCount = READ_BYTE();
				ObjClass* superclass = AS_CLASS(pop(vm));
				if (!invokeFromClass(vm, AS_INSTANCE(vm->frame->slots[0]), superclass, method, argCount)) {
					return STATUS_RUNTIME_ERR;
				}
				vm->frame = &vm->frames[vm->frameCount - 1];
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
						pop(vm);
						pop(vm);
						if(!throwException(vm, "UndefinedPropertyException", "Undefined Property '%s'", name->chars)) return STATUS_RUNTIME_ERR;
						break;
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
					if (!throwException(vm, "InvalidOperationException", "Only instances can contain properties.")) return STATUS_RUNTIME_ERR;
					break;
				}
			}

			case OP_SET_PROPERTY: {
				if (!IS_INSTANCE(peek(vm, 1))) {
					if (!throwException(vm, "InvalidOperationException", "Only instances can contain properties.")) return STATUS_RUNTIME_ERR;
					break;
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
						if (!throwException(vm, "InvalidIndexException", "Can only index an instance using a string.")) return STATUS_RUNTIME_ERR;
						break;
					}

					ObjString* name = AS_STRING(pop(vm));

					Value value;
					if (tableGet(&instance->fields, name, &value)) {
						pop(vm); // Instance.
						push(vm, value);
						break;
					}
					if (!bindMethod(vm, instance->class, name)) {
						pop(vm);
						pop(vm);
						if (!throwException(vm, "UndefinedPropertyException", "Undefined Property '%s'", name->chars)) return STATUS_RUNTIME_ERR;
					}
					break;

				}

				if (IS_STRING(peek(vm, 1))) {
					ObjString* string = AS_STRING(peek(vm, 1));

					if (!IS_NUMBER(peek(vm, 0)) || ceil(AS_NUMBER(peek(vm, 0))) != AS_NUMBER(peek(vm, 0))) {
						if (!throwException(vm, "InvalidIndexException", "Can only index strings using an integer.")) return STATUS_RUNTIME_ERR;
						break;
					}

					double dindex = AS_NUMBER(peek(vm, 0));

					int index = (int)dindex;

					if (index < 0) {
						size_t absIndex = -index; // Index will always be negative so negating gives absolute
						if (absIndex > string->length) {
							if (!throwException(vm, "IndexOutOfBoundsException", "Absolute index is larger than string length.")) return STATUS_RUNTIME_ERR;
							break;
						}

						Value v = OBJ_VAL(copyString(vm, &string->chars[string->length - absIndex], 1));
						pop(vm);
						pop(vm);
						push(vm, v);
						break;
					}

					size_t uIndex = (size_t)index;

					if (uIndex >= string->length) {
						if (!throwException(vm, "IndexOutOfBoundsException", "Index is larger than string length.")) return STATUS_RUNTIME_ERR;
						break;
					}

					Value v = OBJ_VAL(copyString(vm, &string->chars[uIndex], 1));
					pop(vm);
					pop(vm);
					push(vm, v);
					break;
				}

				if (!IS_LIST(peek(vm, 1))) {
					if (!throwException(vm, "InvalidOperationException", "Can only index into lists.")) return STATUS_RUNTIME_ERR;
					break;
				}

				ObjList* list = AS_LIST(peek(vm, 1));

				if (!IS_NUMBER(peek(vm, 0)) || ceil(AS_NUMBER(peek(vm, 0))) != AS_NUMBER(peek(vm, 0))) {
					if (!throwException(vm, "InvalidIndexException", "Can only index a list using an integer.")) return STATUS_RUNTIME_ERR;
					break;
				}

				double dindex = AS_NUMBER(peek(vm, 0));
				int index = (int)dindex;

				if (index < 0) {
					size_t absIndex = -index; // Index will always be negative so negating gives absolute
					if (absIndex > list->items.count) {
						if (!throwException(vm, "IndexOutOfBoundsException", "Absolute index is larger than list length.")) return STATUS_RUNTIME_ERR;
						break;
					}

					Value v = list->items.values[list->items.count - absIndex];
					pop(vm);
					pop(vm);
					push(vm, v);
					break;
				}

				size_t uIndex = (size_t)index;

				if (uIndex >= list->items.count) {
					if (!throwException(vm, "IndexOutOfBoundsException", "Index is larger than list length.")) return STATUS_RUNTIME_ERR;
					break;
				}

				Value v = list->items.values[uIndex];
				pop(vm);
				pop(vm);
				push(vm, v);
				break;
			}

			case OP_SET_INDEX: {

				if (IS_INSTANCE(peek(vm, 2))) {

					ObjInstance* instance = AS_INSTANCE(peek(vm, 2));

					if (!IS_STRING(peek(vm, 1))) {
						if (!throwException(vm, "InvalidIndexException", "Can only index an instance using a string.")) return STATUS_RUNTIME_ERR;
						break;
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
					if (!throwException(vm, "InvalidOperationException", "Can only index into lists.")) return STATUS_RUNTIME_ERR;
					break;
				}

				ObjList* list = AS_LIST(peek(vm, 2));

				if (!IS_NUMBER(peek(vm, 1)) || ceil(AS_NUMBER(peek(vm, 1))) != AS_NUMBER(peek(vm, 1))) {
					if (!throwException(vm, "InvalidIndexException", "Can only index a list using an integer.")) return STATUS_RUNTIME_ERR;
					break;
				}

				double dindex = AS_NUMBER(peek(vm, 1));

				int index = (int)dindex;

				if (index < 0) {
					size_t absIndex = -index; // Index will always be negative so negating gives absolute
					if (absIndex > list->items.count) {
						if (!throwException(vm, "IndexOutOfBoundsException", "Absolute index is larger than list length.")) return STATUS_RUNTIME_ERR;
						break;
					}

					Value v = pop(vm);
					list->items.values[list->items.count - absIndex] = v;
					pop(vm);
					pop(vm);
					push(vm, v);
					break;
				}

				size_t uIndex = (size_t)index;

				if (uIndex >= list->items.count) {
					if (!throwException(vm, "IndexOutOfBoundsException", "Index is larger than list length.")) return STATUS_RUNTIME_ERR;
					break;
				}

				Value v = pop(vm);
				list->items.values[uIndex] = v;
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
						free(string);
						if (!throwException(vm, "InvalidImportException", "Could not find import '%s'.", path->chars)) return STATUS_RUNTIME_ERR;
						break;
					}
				}

				break;
			}

			case OP_IMPORT_STAR: {
					// Dupicate code is bad. 
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
						ObjInstance* obj = AS_INSTANCE(object);

						for (int i = 0; i <= obj->fields.capacity; i++) {
							Entry* entry = &obj->fields.entries[i];
							if (entry->key != NULL) {
								// Copying the string due to interning making them different
								tableSet(vm, &vm->globals, copyString(vm, entry->key->chars, entry->key->length), entry->value);
							}
						}
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
							
							ObjInstance* obj = AS_INSTANCE(object);

							for (int i = 0; i <= obj->fields.capacity; i++) {
								Entry* entry = &obj->fields.entries[i];
								if (entry->key != NULL) {
									// Copying the string due to interning making them different
									tableSet(vm, &vm->globals, copyString(vm, entry->key->chars, entry->key->length), entry->value);
								}
							}

							free(string);
						}

						else {
							free(string);
							if (!throwException(vm, "InvalidImportException", "Could not find import '%s'.", path->chars)) return STATUS_RUNTIME_ERR;
							break;
						}
					}

					break;
				}

			case OP_TYPEOF: {
				Value value = pop(vm);
				
				// These could be made to make copied strings for each case to avoid strlen (or added to VM)
				char* stringRep = NULL;

				if (!IS_OBJ(value)) {
					switch (value.type) {
						case VAL_BOOL: stringRep = "boolean"; break;
						case VAL_NUMBER: stringRep = "number"; break;
						case VAL_NULL: stringRep = "null"; break;
					}
				}
				else {
					switch (AS_OBJ(value)->type) {
						case OBJ_CLOSURE:
						case OBJ_BOUND_METHOD:
						case OBJ_NATIVE:
						case OBJ_FUNCTION:
							stringRep = "function"; break;
						case OBJ_CLASS: stringRep = "class"; break;
						case OBJ_INSTANCE: stringRep = "object"; break;
						case OBJ_STRING: stringRep = "string"; break;
						case OBJ_LIST: stringRep = "list"; break;
					}
				}

				push(vm, OBJ_VAL(copyString(vm, stringRep, strlen(stringRep))));

				break;
			}

			case OP_IMPLEMENTS: {
				Value b = pop(vm);
				Value a = pop(vm);

				if (!IS_CLASS(b)) {
					if (!throwException(vm, "InvalidOperationException", "Right hand operand of an implements clause must be a class.")) return STATUS_RUNTIME_ERR;
					break;
				}
				if (!IS_INSTANCE(a)) {
					push(vm, BOOL_VAL(false));
					break;
				}

				ObjClass* class = AS_CLASS(b);
				ObjInstance* inst = AS_INSTANCE(a);

				for (int i = 0; i <= class->methods.capacity; i++) {
					Entry* entry = &class->methods.entries[i];
					if (entry->key != NULL) {
						Value v;
						bool instHasMethod = tableGet(&inst->class->methods, entry->key, &v);
						if (!instHasMethod) {
							push(vm, BOOL_VAL(false));
							goto loopExit;
						}
					}
				}

				push(vm, BOOL_VAL(true));

				loopExit:

				break;
			}

			case OP_THROW: {
				Value throwee = pop(vm);

				if(!IS_INSTANCE(throwee)) {
					ObjInstance* inst = newInstance(vm, vm->exceptionClass);

					tableSet(vm, &inst->fields, copyString(vm, "value", 5), throwee);

					throwee = OBJ_VAL(inst);
				}
				if (!throwGeneral(vm, AS_INSTANCE(throwee))) return STATUS_RUNTIME_ERR;

				break;
			}

			case OP_TRY_BEGIN: {
				uint16_t catchLocation = READ_SHORT();

				vm->frame->isTry = true;
				vm->frame->catchJump = vm->frame->ip + catchLocation;
				
				break;
			}

			case OP_TRY_END: {
				vm->frame->isTry = false;
				break;
			}

			case OP_RETURN: {
				Value result = pop(vm);

				closeUpvalues(vm, vm->frame->slots);

				vm->frameCount--;
				if (vm->frameCount == 0) {
					pop(vm);
					return STATUS_OK;
				}

				vm->stackTop = vm->frame->slots;
				push(vm, result);

				vm->frame = &vm->frames[vm->frameCount - 1];

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
	initVM(&vm, "main");

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
	initVM(vm, "module");

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

	File file = readFile(path);
	char* source = file.contents;
	if (file.isError) { 
		fprintf(stderr, "%s\n", file.contents);
		free(file.contents);
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
	free(vm->frames);
	free(vm->stack);
	if (vm->isImport) free(vm);
}