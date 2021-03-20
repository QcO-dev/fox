#pragma once
#include <core/common.h>
#include <vm/value.h>
#include <vm/chunk.h>
#include <vm/table.h>

typedef struct VM VM;

typedef enum {
	OBJ_CLOSURE,
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_FUNCTION,
	OBJ_UPVALUE,
	OBJ_CLASS,
	OBJ_INSTANCE,
	OBJ_BOUND_METHOD,
	OBJ_LIST
} ObjType;

struct Obj {
	ObjType type;
	bool isMarked;
	struct Obj* next; // Used to free objects after execution. Single Linked list
};

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))

#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_NATIVE_OBJ(value) ((ObjNative*)AS_OBJ(value))

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))

#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))

#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))

#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define AS_LIST(value) ((ObjList*)AS_OBJ(value))

typedef struct {
	Obj obj;
	size_t arity;
	size_t upvalueCount;
	bool lambda;
	bool varArgs;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

ObjFunction* newFunction(struct VM* vm);

typedef Value(*NativeFn)(VM* vm, size_t argCount, Value* args, Value* bound, bool* hasError);

typedef struct {
	Obj obj;
	size_t arity;
	bool varArgs;
	NativeFn function;
	Value bound;
	bool isBound;
} ObjNative;

ObjNative* newNative(VM* vm, NativeFn function, size_t arity, bool varArgs);

typedef struct ObjUpvalue {
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
} ObjUpvalue;

ObjUpvalue* newUpvalue(VM* vm, Value* slot);

typedef struct {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	size_t upvalueCount;
} ObjClosure;

ObjClosure* newClosure(VM* vm, ObjFunction* function);

struct ObjString {
	Obj obj;
	size_t length;
	char* chars;
	uint32_t hash;
};

ObjString* copyString(struct VM* vm, const char* chars, size_t length);

char* objectToString(VM* vm, Value value);

ObjString* takeString(struct VM* vm, char* chars, int length);

typedef struct {
	Obj obj;
	ObjString* name;
	Table methods;
} ObjClass;

ObjClass* newClass(VM* vm, ObjString* name);

typedef struct {
	Obj obj;
	ObjClass* class;
	Table fields;
} ObjInstance;

ObjInstance* newInstance(VM* vm, ObjClass* class);

typedef struct {
	Obj obj;
	Value receiver;
	ObjClosure* method;
} ObjBoundMethod;

ObjBoundMethod* newBoundMethod(VM* vm, Value receiver, ObjClosure* method);

typedef struct {
	Obj obj;
	ValueArray items;
} ObjList;

ObjList* newList(VM* vm, ValueArray items);