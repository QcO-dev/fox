#include "exception.h"
#include <vm/value.h>
#include <vm/object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value exceptionGetStackTrace(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError) {
	
	ObjInstance* exception = AS_INSTANCE(*bound);

	Table* fields = &exception->fields;

	Value stackTraceValue;
	tableGet(fields, copyString(vm, "stack", 5), &stackTraceValue);

	ValueArray stackTrace = AS_LIST(stackTraceValue)->items;

	char* string = NULL;

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

	Value filename;
	char* filenameString = "<missing field>";
	if (tableGet(fields, copyString(vm, "filename", 8), &filename)) {
		filenameString = valueToString(vm, filename);
	}

	size_t size = snprintf(NULL, 0, "%s in %s: %s", nameString == NULL ? "Exception" : nameString, filenameString, valueString);

	string = malloc(size + 1);
	sprintf(string, "%s in %s: %s", nameString == NULL ? "Exception" : nameString, filenameString, valueString);

	for (size_t i = 0; i < stackTrace.count; i++) {
		size_t lineSize = snprintf(NULL, 0, "\n%s", AS_CSTRING(stackTrace.values[i]));
		string = realloc(string, size + lineSize + 1);
		sprintf(string + size, "\n%s", AS_CSTRING(stackTrace.values[i]));
		size += lineSize;
	}

	return OBJ_VAL(takeString(vm, string, size));
}

void defineExceptionMethods(VM* vm, ObjClass* klass) {
	defineNative(vm, &klass->methods, "getStackTrace", exceptionGetStackTrace, 0, false);
}