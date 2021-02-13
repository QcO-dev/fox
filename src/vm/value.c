#include "value.h"
#include <core/common.h>
#include <core/memory.h>
#include <compiler/compiler.h>
#include <vm/vm.h>
#include <vm/object.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void initValueArray(ValueArray* array) {
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

void writeValueArray(VM* vm, ValueArray* array, Value value) {
	if (array->capacity < array->count + 1) {
		size_t oldCap = array->capacity;
		array->capacity = array->capacity < 8 ? 8 : array->capacity * 2;
		array->values = GROW_ARRAY(vm, Value, array->values, oldCap, array->capacity);
	}

	array->values[array->count++] = value;
}

void freeValueArray(VM* vm, ValueArray* array) {
	FREE_ARRAY(vm, Value, array->values, array->capacity);
	initValueArray(array);
}

// Falsey values
bool isFalsey(Value value) {
	return IS_NULL(value) // null == false
		|| (IS_BOOL(value) && !AS_BOOL(value)) // false == false
		|| (IS_NUMBER(value) && !AS_NUMBER(value)); // 0 == false
}

bool valuesEqual(Value a, Value b) {
	if (a.type != b.type) return false;

	switch (a.type) {
		case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NULL: return true;
		case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
		default:
			return false; // Unreachable.
	}
}

char* valueToString(Value value) {
	
	switch (value.type) {

		case VAL_BOOL: {
			// We use malloc and strcpy here because later in the code it is freed, you can't free a constant string
			char* buffer = malloc((AS_BOOL(value) ? 4 : 5) * sizeof(char) + 1);
			strcpy(buffer, value.boolean ? "true" : "false");
			return buffer;
		}

		case VAL_NULL: {
			char* buffer = malloc(4 * sizeof(char) + 1);
			strcpy(buffer, "null");
			return buffer;
		}

		case VAL_NUMBER: {
			size_t sizeNeeded = snprintf(NULL, 0, "%g", AS_NUMBER(value)) + 1;
			char* buffer = malloc(sizeNeeded);
			sprintf(buffer, "%g", AS_NUMBER(value));
			return buffer;
		}
		case VAL_OBJ: {
			return objectToString(value);
		}
	}
	return NULL; // Unreachable
}
