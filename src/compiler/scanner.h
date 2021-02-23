#pragma once
#include <core/common.h>
typedef struct {
	const char* start;
	const char* current;
	size_t line;
} Scanner;

typedef enum {
	// Single-character tokens.
	TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
	TOKEN_LEFT_SQBR, TOKEN_RIGHT_SQBR,
	TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
	TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_COLON,

	// One or two character tokens.
	TOKEN_BANG, TOKEN_BANG_EQUAL,
	TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_RSH, TOKEN_ASH,
	TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_LSH,
	TOKEN_BIT_AND, TOKEN_AND,
	TOKEN_BIT_OR, TOKEN_OR,
	TOKEN_BIT_NOT, TOKEN_XOR,

	// Literals.
	TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

	// Keywords.
	TOKEN_CLASS, TOKEN_ELSE, TOKEN_EXTENDS, TOKEN_FALSE,
	TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF, TOKEN_NULL,
	TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS, TOKEN_IS,
	TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE, TOKEN_IMPORT, TOKEN_EXPORT,
	TOKEN_AS, TOKEN_FROM, TOKEN_IN,

	TOKEN_ERROR,
	TOKEN_EOF
} TokenType;

typedef struct {
	TokenType type;
	const char* start;
	int length;
	size_t line;
} Token;

void initScanner(Scanner* scanner, const char* source);

Token scanToken(Scanner* scanner);