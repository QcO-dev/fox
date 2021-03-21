#include "iterator.h"
#include <vm/vm.h>
#include <math.h>

Value iteratorInitializer(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ObjInstance* instance = AS_INSTANCE(*bound);

	tableSet(vm, &instance->fields, copyString(vm, "index", 5), NUMBER_VAL(0));
	
	tableSet(vm, &instance->fields, copyString(vm, "data", 4), args[0]);

	return OBJ_VAL(instance);
}

Value iteratorIterator(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	return *bound;
}

Value iteratorNext(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {

	ObjInstance* instance = AS_INSTANCE(*bound);

	Value data;
	if (!tableGet(&instance->fields, copyString(vm, "data", 4), &data)) {
		runtimeError(vm, "Iterator object must have a 'data' property.");
		*hasError = true;
		return NULL_VAL;
	}

	Value indexValue;
	if (!tableGet(&instance->fields, copyString(vm, "index", 5), &indexValue)) {
		runtimeError(vm, "Iterator object must have an 'index' property.");
		*hasError = true;
		return NULL_VAL;
	}

	if (!IS_NUMBER(indexValue) || ceil(AS_NUMBER(indexValue)) != AS_NUMBER(indexValue)) {
		runtimeError(vm, "Iterator object's 'index' must be an integer.");
		*hasError = true;
		return NULL_VAL;
	}
	double dIndex = AS_NUMBER(indexValue);

	size_t index = (size_t)dIndex;

	Value returnValue;

	if (IS_LIST(data)) {

		ObjList* list = AS_LIST(data);

		if (index >= list->items.count) {
			runtimeError(vm, "Iterator object's 'index' cannot be larger than the length (%d >= %d).", index, list->items.count);
			*hasError = true;
			return NULL_VAL;
		}

		returnValue = list->items.values[index];
	}
	else if (IS_STRING(data)) {

		ObjString* string = AS_STRING(data);

		if (index >= string->length) {
			runtimeError(vm, "Iterator object's 'index' cannot be larger than the length (%d >= %d).", index, string->length);
			*hasError = true;
			return NULL_VAL;
		}

		returnValue = OBJ_VAL(copyString(vm, &string->chars[index], 1));
	}
	else {
		runtimeError(vm, "Iterator object's 'data' must be a list or a string.");
		*hasError = true;
		return NULL_VAL;
	}

	tableSet(vm, &instance->fields, copyString(vm, "index", 5), NUMBER_VAL(index + 1));

	return returnValue;
}

Value iteratorDone(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	
	ObjInstance* instance = AS_INSTANCE(*bound);

	Value data;
	if (!tableGet(&instance->fields, copyString(vm, "data", 4), &data)) {
		runtimeError(vm, "Iterator object must have a 'data' property.");
		*hasError = true;
		return NULL_VAL;
	}

	Value indexValue;
	if (!tableGet(&instance->fields, copyString(vm, "index", 5), &indexValue)) {
		runtimeError(vm, "Iterator object must have an 'index' property.");
		*hasError = true;
		return NULL_VAL;
	}

	if (!IS_NUMBER(indexValue) || ceil(AS_NUMBER(indexValue)) != AS_NUMBER(indexValue)) {
		runtimeError(vm, "Iterator object's 'index' must be an integer.");
		*hasError = true;
		return NULL_VAL;
	}
	double dIndex = AS_NUMBER(indexValue);

	size_t index = (size_t)dIndex;
	
	if (IS_LIST(data)) {
		return BOOL_VAL(index >= AS_LIST(data)->items.count);
	}
	else if (IS_STRING(data)) {
		return BOOL_VAL(index >= AS_STRING(data)->length);
	}
	else {
		runtimeError(vm, "Iterator object's 'data' must be a list or a string.");
		*hasError = true;
		return NULL_VAL;
	}
}

void defineIteratorMethods(VM* vm, ObjClass* klass) {
	defineNative(vm, &klass->methods, "<iterator>", iteratorInitializer, 1, false);
	defineNative(vm, &klass->methods, "iterator", iteratorIterator, 0, false);
	defineNative(vm, &klass->methods, "next", iteratorNext, 0, false);
	defineNative(vm, &klass->methods, "done", iteratorDone, 0, false);
}