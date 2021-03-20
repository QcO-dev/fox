#include "objectNative.h"
#include <vm/vm.h>

Value objectKeysNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ValueArray array;
	initValueArray(&array);

	ObjInstance* inst = AS_INSTANCE(*bound);

	for (int i = 0; i <= inst->fields.capacity; i++) {
		Entry* entry = &inst->fields.entries[i];
		if (entry->key != NULL) {
			writeValueArray(vm, &array, OBJ_VAL(entry->key));
		}
	}

	ObjList* list = newList(vm, array);
	return OBJ_VAL(list);
}

Value objectValuesNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	ValueArray array;
	initValueArray(&array);

	ObjInstance* inst = AS_INSTANCE(*bound);

	for (int i = 0; i <= inst->fields.capacity; i++) {
		Entry* entry = &inst->fields.entries[i];
		if (entry->key != NULL) {
			writeValueArray(vm, &array, entry->value);
		}
	}

	ObjList* list = newList(vm, array);
	return OBJ_VAL(list);
}

Value objectHasPropNative(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	if (!IS_STRING(args[0])) {
		runtimeError(vm, "Expected first parameter to be a string.\nin hasProp");
		*hasError = true;
		return NULL_VAL;
	}

	Value v;
	return BOOL_VAL(tableGet(&AS_INSTANCE(*bound)->fields, AS_STRING(args[0]), &v));
}

void defineObjectMethods(VM* vm, ObjClass* klass) {
	defineNative(vm, &klass->methods, "keys", objectKeysNative, 0, false);
	defineNative(vm, &klass->methods, "values", objectValuesNative, 0, false);
	defineNative(vm, &klass->methods, "hasProp", objectHasPropNative, 1, false);
}