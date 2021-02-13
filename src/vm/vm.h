#pragma once
#include <core/common.h>
#include <vm/chunk.h>
#include <vm/table.h>
#include <vm/object.h>

typedef struct Compiler Compiler;

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_MAX + 1)

typedef enum {
	STATUS_OK,
	STATUS_COMPILE_ERR,
	STATUS_RUNTIME_ERR
} InterpreterResult;

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots;
} CallFrame;

struct VM {
	Compiler* compiler;
	CallFrame frames[FRAMES_MAX]; // Change
	int frameCount;
	Value stack[STACK_MAX]; // TODO: Dynamic stack
	Value* stackTop;
	Obj* objects;
	Table strings;
	Table globals;
	Table stringMethods;
	Table listMethods;
	ObjClass* objectClass;
	ObjUpvalue* openUpvalues;
	size_t grayCount;
	size_t grayCapacity;
	Obj** grayStack;
	size_t bytesAllocated;
	size_t nextGC;
};

void initVM(VM* vm);

InterpreterResult execute(VM* vm, Chunk* chunk);

InterpreterResult interpret(const char* source);

InterpreterResult interpretVM(VM* vm, const char* source);

void freeVM(VM* vm);

Value pop(VM* vm);

void push(VM* vm, Value value);

void runtimeError(VM* vm, const char* format, ...);

bool callValue(VM* vm, Value callee, size_t argCount, bool nativeNonGlobal);

Value peek(VM* vm, int distance);