#pragma once
#include <vm/chunk.h>

void disassembleChunk(VM* vm, Chunk* chunk, const char* name);
size_t disassembleInstruction(VM* vm, Chunk* chunk, size_t offset);
size_t getLine(LineNumberTable* table, size_t index);