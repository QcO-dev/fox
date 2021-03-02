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
	CallFrame* frame;
	int frameCount;
	Value stack[STACK_MAX]; // TODO: Dynamic stack
	Value* stackTop;
	Obj* objects;
	Table strings;
	Table globals;
	Table exports;
	Table stringMethods;
	Table listMethods;
	ObjClass* objectClass;
	ObjClass* importClass;
	ObjUpvalue* openUpvalues;
	size_t grayCount;
	size_t grayCapacity;
	Obj** grayStack;
	size_t bytesAllocated;
	size_t nextGC;
	ObjString* basePath;
	ObjString* filepath;
	char* filename;
	size_t importCount;
	size_t importCapacity;
	struct VM** imports;
	bool isImport;
};

void initVM(VM* vm);

InterpreterResult execute(VM* vm, Chunk* chunk);

InterpreterResult interpret(char* basePath, char* filename, const char* source);

InterpreterResult interpretVM(VM* vm, char* basePath, char* filename, const char* source);

void freeVM(VM* vm);

Value pop(VM* vm);

void push(VM* vm, Value value);

void runtimeError(VM* vm, const char* format, ...);

bool callValue(VM* vm, Value callee, size_t argCount);

bool invoke(VM* vm, ObjString* name, int argCount);

Value peek(VM* vm, int distance);