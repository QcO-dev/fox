#pragma once
#include "object.h"
#include <core/common.h>
#include <core/memory.h>
#include <vm/vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <vm/table.h>
#include <debug/debugFlags.h>

#define ALLOCATE_OBJ(vm, type, objectType) \
    (type*)allocateObject(vm, sizeof(type), objectType)

Obj* allocateObject(VM* vm, size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(vm, NULL, 0, size);
	object->type = type;
	object->isMarked = false;
	object->next = vm->objects;
	vm->objects = object;

#ifdef FOX_DEBUG_LOG_GC
	printf("%p allocate %ld for %d\n", (void*)object, size, type);
#endif

	return object;
}

static ObjString* allocateString(VM* vm, char* chars, size_t length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push(vm, OBJ_VAL(string));
	tableSet(vm, &vm->strings, string, NULL_VAL);
	pop(vm);

	return string;
}

//FNV-1a
static uint32_t hashString(const char* key, int length) {
	uint32_t hash = 2166136261u;

	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}

	return hash;
}

ObjString* takeString(VM* vm, char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	ObjString* interned = tableFindString(&vm->strings, chars, length,
		hash);
	if (interned != NULL) {
		FREE_ARRAY(vm, char, chars, length + 1);
		return interned;
	}

	return allocateString(vm, chars, length, hash);
}

ObjString* copyString(VM* vm, const char* chars, size_t length) {

	uint32_t hash = hashString(chars, length);

	ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
	if (interned != NULL) return interned;

	char* heapChars = ALLOCATE(vm, char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(vm, heapChars, length, hash);
}

ObjFunction* newFunction(VM* vm) {
	ObjFunction* function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);

	function->arity = 0;
	function->name = NULL;
	function->upvalueCount = 0;
	initChunk(&function->chunk);
	return function;
}

ObjNative* newNative(VM* vm, NativeFn function, size_t arity) {
	ObjNative* native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
	native->function = function;
	native->arity = arity;
	return native;
}

ObjClosure* newClosure(VM* vm, ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(vm, ObjUpvalue*, function->upvalueCount);
	for (size_t i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}
	ObjClosure* closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjUpvalue* newUpvalue(VM* vm, Value* slot) {
	ObjUpvalue* upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	upvalue->next = NULL;
	upvalue->closed = NULL_VAL;
	return upvalue;
}


ObjClass* newClass(VM* vm, ObjString* name) {
	ObjClass* class = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
	class->name = name;
	initTable(&class->methods);
	return class;
}

ObjInstance* newInstance(VM* vm, ObjClass* class) {
	ObjInstance* instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
	instance->class = class;
	initTable(&instance->fields);
	return instance;
}

ObjBoundMethod* newBoundMethod(VM* vm, Value receiver, ObjClosure* method) {
	ObjBoundMethod* bound = ALLOCATE_OBJ(vm, ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

ObjList* newList(VM* vm, ValueArray items) {
	ObjList* list = ALLOCATE_OBJ(vm, ObjList, OBJ_LIST);
	list->items = items;
	return list;
}

char* functionToString(ObjFunction* value) {
	if (value->name == NULL) {
		size_t sizeNeeded = 8 + 1;
		char* buffer = malloc(sizeNeeded);
		sprintf(buffer, "<script>");
		return buffer;
	}

	size_t sizeNeeded = snprintf(NULL, 0, "<function %s>", value->name->chars) + 1;
	char* buffer = malloc(sizeNeeded);
	sprintf(buffer, "<function %s>", value->name->chars);
	return buffer;
}

//TODO REPR functions
char* objectToString(Value value) {
	switch (OBJ_TYPE(value)) {

		case OBJ_LIST: {
			ObjList* list = AS_LIST(value);

			size_t itemLengths = 1;

			for (size_t i = 0; i < list->items.count; i++) {
				itemLengths += snprintf(NULL, 0, i == list->items.count - 1 ? "%s" : "%s, ", valueToString(list->items.values[i]));
			}

			itemLengths += 1;

			char* buffer = malloc(itemLengths + 1);

			buffer[0] = '[';

			size_t index = 1;
			for (size_t i = 0; i < list->items.count; i++) {
				index += sprintf(&buffer[index], i == list->items.count - 1 ? "%s" : "%s, ", valueToString(list->items.values[i]));
			}

			buffer[itemLengths - 1] = ']';
			buffer[itemLengths] = '\0';
			return buffer;
		}

		case OBJ_CLASS: {
			size_t sizeNeeded = snprintf(NULL, 0, "<class %s>", AS_CLASS(value)->name->chars) + 1;
			char* buffer = malloc(sizeNeeded);
			sprintf(buffer, "<class %s>", AS_CLASS(value)->name->chars);
			return buffer;
		}

		case OBJ_INSTANCE: {
			size_t sizeNeeded = snprintf(NULL, 0, "<instance %s>", AS_INSTANCE(value)->class->name->chars) + 1;
			char* buffer = malloc(sizeNeeded);
			sprintf(buffer, "<instance %s>", AS_INSTANCE(value)->class->name->chars);
			return buffer;
		}

		case OBJ_STRING: {
			size_t sizeNeeded = snprintf(NULL, 0, "%s", AS_CSTRING(value)) + 1;
			char* buffer = malloc(sizeNeeded);
			sprintf(buffer, "%s", AS_CSTRING(value));
			return buffer;
		}

		case OBJ_FUNCTION: {
			return functionToString(AS_FUNCTION(value));
		}

		case OBJ_CLOSURE:
			return functionToString(AS_CLOSURE(value)->function);

		case OBJ_BOUND_METHOD:
			return functionToString(AS_BOUND_METHOD(value)->method->function);

		case OBJ_NATIVE: {
			size_t sizeNeeded = 17 + 1;
			char* buffer = malloc(sizeNeeded);
			strcpy(buffer, "<native function>");
			return buffer;
		}
	}
	return NULL; // Unreachable
}
