#pragma once
typedef enum {
	OP_CONSTANT, // OP_CONSTANT_W
	OP_NULL,
	OP_TRUE,
	OP_FALSE,
	OP_NEGATE,
	OP_ADD,
	OP_SUB,
	OP_DIV,
	OP_MUL,
	OP_NOT,
	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_GREATER_EQ,
	OP_LESS_EQ,
	OP_BITWISE_NOT,
	OP_XOR,
	OP_BITWISE_AND,
	OP_BITWISE_OR,
	OP_LSH,
	OP_RSH,
	OP_ASH,
	OP_POP,
	OP_PRINT,
	OP_DEFINE_GLOBAL,
	OP_GET_GLOBAL,
	OP_SET_GLOBAL,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_UPVALUE,
	OP_SET_UPVALUE,
	OP_GET_PROPERTY,
	OP_SET_PROPERTY,
	OP_SET_PROPERTY_V,
	OP_JUMP_IF_FALSE,
	OP_JUMP_IF_FALSE_S,
	OP_JUMP,
	OP_LOOP,
	OP_CALL,
	OP_CLOSURE,
	OP_CLOSE_UPVALUE,
	OP_CLASS,
	OP_METHOD,
	OP_INVOKE,
	OP_INHERIT,
	OP_GET_SUPER,
	OP_SUPER_INVOKE,
	OP_OBJECT,
	OP_LIST,
	OP_GET_INDEX,
	OP_SET_INDEX,
	OP_EXPORT,
	OP_IMPORT,
	OP_RETURN
} Opcode;