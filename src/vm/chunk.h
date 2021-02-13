#pragma once
#include <core/common.h>
#include <vm/lineNumber.h>
#include <vm/value.h>

typedef struct VM VM;

typedef struct {
	size_t count;
	size_t capacity;
	uint8_t* code;
	LineNumberTable table;
	ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);

void writeChunk(VM* vm, Chunk* chunk, uint8_t byte, size_t lineNumber);

void freeChunk(VM* vm, Chunk* chunk);

size_t addConstant(VM* vm, Chunk* chunk, Value value);