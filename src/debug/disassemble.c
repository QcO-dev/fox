#include "disassemble.h"
#include <stdio.h>
#include <vm/opcodes.h>
#include <vm/value.h>
#include <stdlib.h>
#include <vm/object.h>
#include <vm/vm.h>

size_t getLine(LineNumberTable* table, size_t index) {
	
	size_t offset = 0;

	while (offset < table->count && table->lines[offset] <= index) {
		offset += 2;
	}

	return table->lines[offset - 1];
}

void disassembleChunk(VM* vm, Chunk* chunk, const char* name) {
	printf("=== %s | %s ===\n", vm->filename, name);

	size_t offset = 0;

	while (offset < chunk->count) {
		offset = disassembleInstruction(chunk, offset);
		printf("\n");
	}

}

static size_t simpleInstruction(const char* name, size_t offset) {
	printf("%.16s", name);
	return offset + 1;
}

static size_t constantInstruction(const char* name, size_t offset, Chunk* chunk) {
	uint8_t constant = chunk->code[offset + 1];
	char* string = valueToString(chunk->constants.values[constant]);
	printf("%-16s %4d '%s'", name, constant, string);
	free(string);
	return offset + 2;
}

static int byteInstruction(const char* name, size_t offset, Chunk* chunk) {
	uint8_t slot = chunk->code[offset + 1];
	printf("%-16s %4d", name, slot);
	return offset + 2;
}

static int jumpInstruction(const char* name, int sign, size_t offset, Chunk* chunk) {
	uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
	jump |= chunk->code[offset + 2];
	printf("%-16s %4d | %d", name, offset, offset + 3 + sign * jump);
	return offset + 3;
}

static int invokeInstruction(const char* name, size_t offset, Chunk* chunk) {
	uint8_t constant = chunk->code[offset + 1];
	uint8_t argCount = chunk->code[offset + 2];
	char* string = valueToString(chunk->constants.values[constant]);
	printf("%-18s (%d args) %4d '%s'", name, argCount, constant, string);
	free(string);
	return offset + 3;
}

static int importInstruction(const char* name, size_t offset, Chunk* chunk) {
	uint8_t constant = chunk->code[offset + 1];
	char* string = valueToString(chunk->constants.values[constant]);

	uint8_t nameConstant = chunk->code[offset + 2];
	char* nameString = valueToString(chunk->constants.values[nameConstant]);

	printf("%-16s %4d '%s' -> %4d '%s'", name, constant, string, nameConstant, nameString);
	free(string);
	return offset + 3;
}

size_t disassembleInstruction(Chunk* chunk, size_t offset) {

	printf("%04d ", offset);
	printf("%4d ", getLine(&chunk->table, offset));

	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
		case OP_RETURN: return simpleInstruction("RETURN", offset);
		case OP_NEGATE: return simpleInstruction("NEGATE", offset);
		case OP_NOT: return simpleInstruction("NOT", offset);
		case OP_BITWISE_NOT: return simpleInstruction("BITWISE_NOT", offset);
		case OP_BITWISE_AND: return simpleInstruction("BITWISE_AND", offset);
		case OP_BITWISE_OR: return simpleInstruction("BITWISE_OR", offset);
		case OP_XOR: return simpleInstruction("XOR", offset);
		case OP_LSH: return simpleInstruction("LSH", offset);
		case OP_RSH: return simpleInstruction("RSH", offset);
		case OP_ASH: return simpleInstruction("ASH", offset);
		case OP_ADD: return simpleInstruction("ADD", offset);
		case OP_SUB: return simpleInstruction("SUB", offset);
		case OP_DIV: return simpleInstruction("DIV", offset);
		case OP_MUL: return simpleInstruction("MUL", offset);
		case OP_NULL: return simpleInstruction("NULL", offset);
		case OP_TRUE: return simpleInstruction("TRUE", offset);
		case OP_FALSE: return simpleInstruction("FALSE", offset);
		case OP_EQUAL: return simpleInstruction("EQUAL", offset);
		case OP_GREATER: return simpleInstruction("GREATER", offset);
		case OP_GREATER_EQ: return simpleInstruction("GREATER_EQ", offset);
		case OP_LESS: return simpleInstruction("LESS", offset);
		case OP_LESS_EQ: return simpleInstruction("LESS_EQ", offset);
		case OP_POP: return simpleInstruction("POP", offset);
		case OP_PRINT: return simpleInstruction("PRINT", offset);
		case OP_CONSTANT: return constantInstruction("CONSTANT", offset, chunk);
		case OP_DEFINE_GLOBAL: return constantInstruction("DEFINE_GLOBAL", offset, chunk);
		case OP_SET_GLOBAL: return constantInstruction("SET_GLOBAL", offset, chunk);
		case OP_GET_GLOBAL: return constantInstruction("GET_GLOBAL", offset, chunk);
		case OP_GET_LOCAL: return byteInstruction("GET_LOCAL", offset, chunk);
		case OP_SET_LOCAL: return byteInstruction("SET_LOCAL", offset, chunk);
		case OP_JUMP: return jumpInstruction("JUMP", 1, offset, chunk);
		case OP_JUMP_IF_FALSE: return jumpInstruction("JUMP_IF_FALSE", 1, offset, chunk);
		case OP_JUMP_IF_FALSE_S: return jumpInstruction("JUMP_IF_FALSE_S", 1, offset, chunk);
		case OP_LOOP: return jumpInstruction("LOOP", -1, offset, chunk);
		case OP_CALL: return byteInstruction("CALL", offset, chunk);
		case OP_CLOSURE: {
			offset++;
			uint8_t constant = chunk->code[offset++];
			printf("%-16s %4d ", "CLOSURE", constant);
			char* string = valueToString(chunk->constants.values[constant]);
			printf("'%s'", string);
			free(string);

			ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
			for (size_t j = 0; j < function->upvalueCount; j++) {
				int isLocal = chunk->code[offset++];
				int index = chunk->code[offset++];
				printf("\n%04d      |                     %s %d",
					offset - 2, isLocal ? "local" : "upvalue", index);
			}

			return offset;
		}
		case OP_GET_UPVALUE: return byteInstruction("GET_UPVALUE", offset, chunk);
		case OP_SET_UPVALUE: return byteInstruction("SET_UPVALUE", offset, chunk);
		case OP_CLOSE_UPVALUE: return simpleInstruction("CLOSE_UPVALUE", offset);
		case OP_CLASS: return constantInstruction("CLASS", offset, chunk);
		case OP_GET_PROPERTY: return constantInstruction("GET_PROPERTY", offset, chunk);
		case OP_SET_PROPERTY: return constantInstruction("SET_PROPERTY", offset, chunk);
		case OP_SET_PROPERTY_V: return constantInstruction("SET_PROPERTY_V", offset, chunk);
		case OP_METHOD: return constantInstruction("METHOD", offset, chunk);
		case OP_INVOKE: return invokeInstruction("INVOKE", offset, chunk);
		case OP_INHERIT: return simpleInstruction("INHERIT", offset);
		case OP_GET_SUPER: return constantInstruction("GET_SUPER", offset, chunk);
		case OP_SUPER_INVOKE: return invokeInstruction("SUPER_INVOKE", offset, chunk);
		case OP_LIST: return byteInstruction("LIST", offset, chunk);
		case OP_GET_INDEX: return simpleInstruction("GET_INDEX", offset);
		case OP_SET_INDEX: return simpleInstruction("SET_INDEX", offset);
		case OP_OBJECT: return simpleInstruction("OBJECT", offset);
		case OP_EXPORT: return constantInstruction("EXPORT", offset, chunk);
		case OP_IMPORT: return importInstruction("IMPORT", offset, chunk);
		default:
			printf("Unknown opcode: %02X", instruction);
			return offset + 1;
	}

}
