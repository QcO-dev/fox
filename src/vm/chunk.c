#include "chunk.h"
#include <core/memory.h>
#include <vm/opcodes.h>

void initChunk(Chunk* chunk) {
	chunk->code = NULL;
	chunk->capacity = 0;
	chunk->count = 0;
	initLineNumberTable(&chunk->table);
	initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, size_t lineNumber) {

	writeLineNumberTable(&chunk->table, chunk->count, lineNumber);

	if (chunk->capacity < chunk->count + 1) {
		size_t oldCapacity = chunk->capacity;
		chunk->capacity = chunk->capacity < 8 ? 8 : chunk->capacity * 2;
		chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
	}

	chunk->code[chunk->count++] = byte;
}

void freeChunk(Chunk* chunk) {
	FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	freeLineNumberTable(&chunk->table);
	freeValueArray(&chunk->constants);
	initChunk(chunk);
}

size_t addConstant(VM* vm, Chunk* chunk, Value value) {
	push(vm, value);
	writeValueArray(&chunk->constants, value);
	pop(vm);
	return chunk->constants.count - 1;
}
