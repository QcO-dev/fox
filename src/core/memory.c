#include "memory.h"
#include <core/common.h>
#include <stdlib.h>
#include <stdio.h>
#include <vm/object.h>
#include <debug/debugFlags.h>
#include <compiler/compiler.h>

#ifdef FOX_DEBUG_LOG_GC
#include <stdio.h>
#endif

#define GC_HEAP_GROW_FACTOR 2

VM* currentVM; // TODO temp code

void setCurrentVMMemory(VM* vm) {
	currentVM = vm;
}

void collectGarbage(VM* vm);
static void freeObject(Obj* object);

void* reallocate(void* pointer, size_t oldSize, size_t size) {

	currentVM->bytesAllocated += size - oldSize;

#ifdef FOX_DEBUG_STRESS_GC
	if (size > oldSize) {
		collectGarbage(currentVM);
	}
#endif

#ifndef FOX_DEBUG_DISABLE_GC
	if (currentVM->bytesAllocated > currentVM->nextGC) {
		collectGarbage(currentVM);
	}
#endif
	if (size == 0) {
		free(pointer);
		return NULL;
	}

	void* p = realloc(pointer, size);

	if (p == NULL) {
		fprintf(stderr, "Failed to reallocate memory.");
		exit(1);
	}

	return p;
}

void markObject(VM* vm, Obj* object) {
	if (object == NULL) return;
	if (object->isMarked) return;

#ifdef FOX_DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	char* string = objectToString(OBJ_VAL(object));
	printf("%s\n", string);
	free(string);
#endif

	object->isMarked = true;

	if (vm->grayCapacity < vm->grayCount + 1) {
		vm->grayCapacity = vm->grayCapacity < 8 ? 8 : vm->grayCapacity * 2;
		vm->grayStack = realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
		if (vm->grayStack == NULL) exit(1);
	}

	vm->grayStack[vm->grayCount++] = object;

}

void markValue(VM* vm, Value value) {
	if (!IS_OBJ(value)) return;
	markObject(vm, AS_OBJ(value));
}

void markTable(VM* vm, Table* table) {
	for (int i = 0; i <= table->capacity; i++) {
		Entry* entry = &table->entries[i];
		markObject(vm, (Obj*)entry->key);
		markValue(vm, entry->value);
	}
}

static void markArray(VM* vm, ValueArray* array) {
	for (size_t i = 0; i < array->count; i++) {
		markValue(vm, array->values[i]);
	}
}

static void blackenObject(VM* vm, Obj* object) {

#ifdef FOX_DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	char* string = objectToString(OBJ_VAL(object));
	printf("%s\n", string);
	free(string);
#endif

	switch (object->type) {

		case OBJ_LIST: {
			ObjList* list = (ObjList*)object;
			markArray(vm, &list->items);
			break;
		}
		case OBJ_CLASS: {
			ObjClass* klass = (ObjClass*)object;
			markObject(vm, (Obj*)klass->name);
			markTable(vm, &klass->methods);
			break;
		}

		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)object;
			markObject(vm, (Obj*)instance->class);
			markTable(vm, &instance->fields);
			break;
		}

		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = (ObjBoundMethod*)object;
			markValue(vm, bound->receiver);
			markObject(vm, (Obj*)bound->method);
			break;
		}

		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			markObject(vm, (Obj*)closure->function);
			for (size_t i = 0; i < closure->upvalueCount; i++) {
				markObject(vm, (Obj*)closure->upvalues[i]);
			}
			break;
		}

		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			markObject(vm, (Obj*)function->name);
			markArray(vm, &function->chunk.constants);
			break;
		}

		case OBJ_UPVALUE:
			markValue(vm, ((ObjUpvalue*)object)->closed);
			break;
		case OBJ_NATIVE:
		case OBJ_STRING:
			break;
	}
}

static void traceReferences(VM* vm) {
	while (vm->grayCount > 0) {
		Obj* object = vm->grayStack[--vm->grayCount];
		blackenObject(vm, object);
	}
}

static void markRoots(VM* vm) {
	for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
		markValue(vm, *slot);
	}

	for (int i = 0; i < vm->frameCount; i++) {
		markObject(vm, (Obj*)vm->frames[i].closure);
	}

	for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
		markObject(vm, (Obj*)upvalue);
	}

	markTable(vm, &vm->globals);
	if(vm->compiler != NULL)
		markCompilerRoots(vm->compiler);
}

static void sweep(VM* vm) {
	Obj* previous = NULL;
	Obj* object = vm->objects;
	while (object != NULL) {
		if (object->isMarked) {
			object->isMarked = false;
			previous = object;
			object = object->next;
		}
		else {
			Obj* unreached = object;
			object = object->next;
			if (previous != NULL) {
				previous->next = object;
			}
			else {
				vm->objects = object;
			}

			freeObject(unreached);
		}
	}
}

void collectGarbage(VM* vm) {
#ifdef FOX_DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm->bytesAllocated;
#endif

	markRoots(vm);

	traceReferences(vm);

	tableRemoveWhite(&vm->strings);

	sweep(vm);

	vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef FOX_DEBUG_LOG_GC
	printf("-- gc end\n");
	printf("   collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif

}

static void freeObject(Obj* object) {

#ifdef FOX_DEBUG_LOG_GC
	printf("%p free type %d\n", (void*)object, object->type);
#endif

	switch (object->type) {

		case OBJ_CLASS: {
			ObjClass* klass = (ObjClass*)object;
			freeTable(&klass->methods);
			FREE(ObjClass, object);
			break;
		}

		case OBJ_LIST: {
			FREE(ObjList, object);
			break;
		}

		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)object;
			freeTable(&instance->fields);
			FREE(ObjInstance, object);
			break;
		}

		case OBJ_BOUND_METHOD:
			FREE(ObjBoundMethod, object);
			break;

		case OBJ_STRING: {
			ObjString* string = (ObjString*)object;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, object);
			break;
		}

		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			freeChunk(&function->chunk);
			FREE(ObjFunction, object);
			break;
		}

		case OBJ_NATIVE: {
			FREE(ObjNative, object);
			break;
		}

		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
			FREE(ObjClosure, object);
			break;
		}

		case OBJ_UPVALUE: {
			FREE(ObjUpvalue, object);
			break;
		}

	}
}

void freeObjects(VM* vm) {
	Obj* object = vm->objects;
	while (object != NULL) {
		Obj* next = object->next;
		freeObject(object);
		object = next;
	}
}
