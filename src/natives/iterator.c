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
		*hasError = !throwException(vm, "UndefinedPropertyException", "Iterator object must have a 'data' property.");
		return pop(vm);
	}

	Value indexValue;
	if (!tableGet(&instance->fields, copyString(vm, "index", 5), &indexValue)) {
		*hasError = !throwException(vm, "UndefinedPropertyException", "Iterator object must have an 'index' property.");
		return pop(vm);
	}

	if (!IS_NUMBER(indexValue) || ceil(AS_NUMBER(indexValue)) != AS_NUMBER(indexValue)) {
		*hasError = !throwException(vm, "TypeException", "Iterator object's 'index' must be an integer.");
		return pop(vm);
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
			*hasError = !throwException(vm, "TypeException", "Iterator object's 'index' must be an integer.");
			return pop(vm);
		}

		returnValue = list->items.values[index];
	}
	else if (IS_STRING(data)) {

		ObjString* string = AS_STRING(data);

		if (index >= string->length) {
			*hasError = !throwException(vm, "InvalidIndexException", "Iterator object's 'index' cannot be larger than the length (%d >= %d).", index, string->length);
			return pop(vm);
		}

		returnValue = OBJ_VAL(copyString(vm, &string->chars[index], 1));
	}
	else {
		*hasError = !throwException(vm, "TypeException", "Iterator object's 'data' must be a list or a string.");
		return pop(vm);
	}

	tableSet(vm, &instance->fields, copyString(vm, "index", 5), NUMBER_VAL(index + 1));

	return returnValue;
}

Value iteratorDone(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	
	ObjInstance* instance = AS_INSTANCE(*bound);

	Value data;
	if (!tableGet(&instance->fields, copyString(vm, "data", 4), &data)) {
		*hasError = !throwException(vm, "UndefinedPropertyException", "Iterator object must have a 'data' property.");
		return pop(vm);
	}

	Value indexValue;
	if (!tableGet(&instance->fields, copyString(vm, "index", 5), &indexValue)) {
		*hasError = !throwException(vm, "UndefinedPropertyException", "Iterator object must have an 'index' property.");
		return pop(vm);
	}

	if (!IS_NUMBER(indexValue) || ceil(AS_NUMBER(indexValue)) != AS_NUMBER(indexValue)) {
		*hasError = !throwException(vm, "TypeException", "Iterator object's 'index' must be an integer.");
		return pop(vm);
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
		*hasError = !throwException(vm, "TypeException", "Iterator object's 'data' must be a list or a string.");
		return pop(vm);
	}
}

void defineIteratorMethods(VM* vm, ObjClass* klass) {
	defineNative(vm, &klass->methods, "Iterator", iteratorInitializer, 1, false);
	defineNative(vm, &klass->methods, "iterator", iteratorIterator, 0, false);
	defineNative(vm, &klass->methods, "next", iteratorNext, 0, false);
	defineNative(vm, &klass->methods, "done", iteratorDone, 0, false);
}