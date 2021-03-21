#include "compiler.h"
#include <compiler/scanner.h>
#include <stdio.h>
#include <vm/opcodes.h>
#include <string.h>
#include <stdlib.h>
#include <vm/vm.h>
#include <vm/object.h>
#include <core/memory.h>
#include <debug/debugFlags.h>
#ifdef FOX_DUMP_CODE
#include <debug/disassemble.h>
#endif

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef struct {
	uint8_t index;
	bool isLocal;
} Upvalue;

typedef enum {
	TYPE_FUNCTION,
	TYPE_METHOD,
	TYPE_INITIALIZER,
	TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
	struct Compiler* enclosing;
	VM* vm;
	ObjFunction* function;
	FunctionType type;

	Local locals[UINT8_MAX + 1];
	Upvalue upvalues[UINT8_MAX + 1];
	int localCount;
	int scopeDepth;
	bool isLoop;
	int continuePoint;
	int breakPoint;
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler* enclosing;
	bool hasSuperclass;
	Token superclass;
	Token name;
} ClassCompiler;

typedef struct {
	Scanner* scanner;
	ClassCompiler* currentClass;
	VM* vm;
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
} Parser;

typedef void (*ParseFn)(Parser* parser, Compiler* compiler, bool canAssign);

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_TERNARY,     // x ? y : z
	PREC_OR,          // ||
	PREC_AND,         // &&
	PREC_BIT_OR,      // |
	PREC_XOR,         // ^
	PREC_BIT_AND,     // &
	PREC_EQUALITY,    // == != is
	PREC_COMPARISON,  // < > <= >= implements
	PREC_SHIFT,       // << >> >>>
	PREC_TERM,        // + -
	PREC_FACTOR,      // * /
	PREC_RANGE,       // x..y
	PREC_UNARY,       // ! - typeof
	PREC_CALL,        // . () []
	PREC_PRIMARY      // x {}
} Precedence;

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

static ParseRule* getRule(TokenType type);
static void expression(Parser* parser, Compiler* compiler);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Parser* parser, Compiler* compiler, Precedence precedence);
static void error(Parser* parser, const char* message);
static void errorAt(Parser* parser, Token* token, const char* message);
static void statement(Parser* parser, Compiler* compiler);
static void declaration(Parser* parser, Compiler* compiler);
static uint8_t identifierConstant(Parser* parser, Compiler* compiler, Token* name);
static bool identifiersEqual(Token* a, Token* b);
static void beginScope(Compiler* compiler);
static void endScope(Parser* parser, Compiler* compiler);
static void varDeclaration(Parser* parser, Compiler* compiler);
static void block(Parser* parser, Compiler* compiler);
static void defineVariable(Parser* parser, Compiler* compiler, uint8_t global);
static uint8_t parseVariable(Parser* parser, Compiler* compiler, const char* errorMessage);
static uint8_t argumentList(Parser* parser, Compiler* compiler);
static void variable(Parser* parser, Compiler* compiler, bool canAssign);
static void namedVariable(Parser* parser, Compiler* compiler, Token name, bool canAssign);
static void and(Parser* parser, Compiler* compiler, bool canAssign);
static void or(Parser * parser, Compiler * compiler, bool canAssign);

static Chunk* currentChunk(Compiler* compiler) {
	return &compiler->function->chunk;
}

static void initCompiler(Compiler* compiler, Compiler* oldCompiler, VM* vm, Parser* parser, FunctionType type) {
	vm->compiler = compiler;
	compiler->vm = vm;
	compiler->enclosing = oldCompiler;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->isLoop = false;
	compiler->function = newFunction(vm);
	compiler->function->lambda = false;
	compiler->function->varArgs = false;

	if (type != TYPE_SCRIPT) {
		compiler->function->name = copyString(vm, parser->previous.start, parser->previous.length);
	}

	Local* local = &compiler->locals[compiler->localCount++];
	local->depth = 0;
	if (type != TYPE_FUNCTION) {
		local->name.start = "this";
		local->name.length = 4;
	}
	else {
		local->name.start = "";
		local->name.length = 0;
	}
	local->isCaptured = false;
}

static void emitByte(Parser* parser, Compiler* compiler, uint8_t byte) {
	writeChunk(compiler->vm, currentChunk(compiler), byte, parser->previous.line);
}

static int emitJump(Parser* parser, Compiler* compiler, uint8_t instruction) {
	emitByte(parser, compiler, instruction);
	emitByte(parser, compiler, 0xff);
	emitByte(parser, compiler, 0xff);
	return currentChunk(compiler)->count - 2;
}

static void emitLoop(Parser* parser, Compiler* compiler, size_t loopStart) {
	emitByte(parser, compiler, OP_LOOP);

	int offset = currentChunk(compiler)->count - loopStart + 2;
	if (offset > UINT16_MAX) error(parser, "Loop body too large.");

	emitByte(parser, compiler, (offset >> 8) & 0xff);
	emitByte(parser, compiler, offset & 0xff);
}

static void patchJump(Parser* parser, Compiler* compiler, size_t offset) {
	// -2 to adjust for the bytecode for the jump offset itself.
	int jump = currentChunk(compiler)->count - offset - 2;

	if (jump > UINT16_MAX) {
		error(parser, "Too much code to jump over.");
	}

	currentChunk(compiler)->code[offset] = (jump >> 8) & 0xff;
	currentChunk(compiler)->code[offset + 1] = jump & 0xff;
}

static void emitReturn(Parser* parser, Compiler* compiler) {
	if (compiler->type == TYPE_INITIALIZER) {
		emitByte(parser, compiler, OP_GET_LOCAL);
		emitByte(parser, compiler, 0);
	}
	else {
		emitByte(parser, compiler, OP_NULL);
	}
	emitByte(parser, compiler, OP_RETURN);
}

static Token syntheticToken(const char* text) {
	Token token;
	token.start = text;
	token.length = (int)strlen(text);
	return token;
}

static ObjFunction* endCompiler(Parser* parser, Compiler* compiler) {
	emitReturn(parser, compiler);
	ObjFunction* function = compiler->function;
#ifdef FOX_DUMP_CODE
	if (!parser->hadError) {
		disassembleChunk(compiler->vm, currentChunk(compiler), function->name != NULL ? function->name->chars : "<script>");
	}
#endif
	return function;
}

static void advance(Parser* parser) {
	parser->previous = parser->current;

	for (;;) {
		parser->current = scanToken(parser->scanner);
		if (parser->current.type != TOKEN_ERROR) break;

		error(parser, parser->current.start);
	}
}

static void consume(Parser* parser, TokenType type, const char* message) {
	if (parser->current.type == type) {
		advance(parser);
		return;
	}

	error(parser, message);
}

static bool match(Parser* parser, TokenType type) {
	if (parser->current.type != type) return false;
	advance(parser);
	return true;
}

static void error(Parser* parser, const char* message) {
	errorAt(parser, &parser->previous, message);
}

static void errorAt(Parser* parser, Token* token, const char* message) {
	if (parser->panicMode) return;
	parser->panicMode = true;
	fprintf(stderr, "[%d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at EOF");
	}
	else if (token->type == TOKEN_ERROR) {
		// Nothing.
	}
	else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser->hadError = true;
}

static uint8_t makeConstant(Parser* parser, Compiler* compiler, Value value) {
	int constant = addConstant(compiler->vm, currentChunk(compiler), value);
	if (constant > UINT8_MAX) {
		error(parser, "Too many constants in one chunk.");
		return 0;
	}

	return (uint8_t)constant;
}

static void emitConstant(Parser* parser, Compiler* compiler, Value value) {

	for (size_t i = 0; i < currentChunk(compiler)->constants.count; i++) { //TODO For constant_w
		if (valuesEqual(currentChunk(compiler)->constants.values[i], value)) {
			emitByte(parser, compiler, OP_CONSTANT);
			emitByte(parser, compiler, (uint8_t) i);
			return;
		}
	}

	emitByte(parser, compiler, OP_CONSTANT);
	emitByte(parser, compiler, makeConstant(parser, compiler, value));
}

static bool isAssignment(Parser* parser) {
	switch (parser->current.type) {
		case TOKEN_IN_PLUS:
		case TOKEN_IN_MINUS:
		case TOKEN_IN_SLASH:
		case TOKEN_IN_STAR:
		case TOKEN_IN_ASH:
		case TOKEN_IN_RSH:
		case TOKEN_IN_LSH:
		case TOKEN_IN_BIT_AND:
		case TOKEN_IN_BIT_OR:
		case TOKEN_IN_XOR:
			advance(parser);
			return true;

		default:
			return false;
	}
}

static void inplaceOperator(Parser* parser, Compiler* compiler, TokenType type) {
	switch (type) {
		case TOKEN_IN_PLUS: emitByte(parser, compiler, OP_ADD); break;
		case TOKEN_IN_MINUS: emitByte(parser, compiler, OP_SUB); break;
		case TOKEN_IN_SLASH: emitByte(parser, compiler, OP_DIV); break;
		case TOKEN_IN_STAR: emitByte(parser, compiler, OP_MUL); break;
		case TOKEN_IN_ASH: emitByte(parser, compiler, OP_ASH); break;
		case TOKEN_IN_RSH: emitByte(parser, compiler, OP_RSH); break;
		case TOKEN_IN_LSH: emitByte(parser, compiler, OP_LSH); break;
		case TOKEN_IN_BIT_AND: emitByte(parser, compiler, OP_BITWISE_AND); break;
		case TOKEN_IN_BIT_OR: emitByte(parser, compiler, OP_BITWISE_OR); break;
		case TOKEN_IN_XOR: emitByte(parser, compiler, OP_XOR); break;
	}
}

static void parsePrecedence(Parser* parser, Compiler* compiler, Precedence precedence) {
	advance(parser);
	ParseFn prefixRule = getRule(parser->previous.type)->prefix;
	if (prefixRule == NULL) {
		error(parser, "Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(parser, compiler, canAssign);

	while (precedence <= getRule(parser->current.type)->precedence) {
		advance(parser);
		ParseFn infixRule = getRule(parser->previous.type)->infix;
		infixRule(parser, compiler, canAssign);
	}

	if (canAssign && (match(parser, TOKEN_EQUAL) || isAssignment(parser))) {
		error(parser, "Invalid assignment target.");
	}

}

static void number(Parser* parser, Compiler* compiler, bool canAssign) {
	double value = strtod(parser->previous.start, NULL);
	emitConstant(parser, compiler, NUMBER_VAL(value));
}

static void unary(Parser* parser, Compiler* compiler, bool canAssign) {
	TokenType operatorType = parser->previous.type;

	// Compile the operand.
	parsePrecedence(parser, compiler, PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
		case TOKEN_MINUS: emitByte(parser, compiler, OP_NEGATE); break;
		case TOKEN_BANG: emitByte(parser, compiler, OP_NOT); break;
		case TOKEN_BIT_NOT: emitByte(parser, compiler, OP_BITWISE_NOT); break;
		case TOKEN_TYPEOF: emitByte(parser, compiler, OP_TYPEOF); break;
		default:
			return; // Unreachable.
	}
}

static void binary(Parser* parser, Compiler* compiler, bool canAssign) {
	TokenType operatorType = parser->previous.type;

	// Compile the right operand.
	ParseRule* rule = getRule(operatorType);
	parsePrecedence(parser, compiler, (Precedence)(rule->precedence + 1));

	// Emit the operator instruction.
	switch (operatorType) {
		case TOKEN_PLUS:          emitByte(parser, compiler, OP_ADD); break;
		case TOKEN_MINUS:         emitByte(parser, compiler, OP_SUB); break;
		case TOKEN_STAR:          emitByte(parser, compiler, OP_MUL); break;
		case TOKEN_SLASH:         emitByte(parser, compiler, OP_DIV); break;
		case TOKEN_BANG_EQUAL:    emitByte(parser, compiler, OP_EQUAL); emitByte(parser, compiler, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL:   emitByte(parser, compiler, OP_EQUAL); break;
		case TOKEN_GREATER:       emitByte(parser, compiler, OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emitByte(parser, compiler, OP_GREATER_EQ); break;
		case TOKEN_LESS:          emitByte(parser, compiler, OP_LESS); break;
		case TOKEN_LESS_EQUAL:    emitByte(parser, compiler, OP_LESS_EQ); break;
		case TOKEN_BIT_NOT: emitByte(parser, compiler, OP_BITWISE_NOT); break;
		case TOKEN_BIT_AND: emitByte(parser, compiler, OP_BITWISE_AND); break;
		case TOKEN_BIT_OR: emitByte(parser, compiler, OP_BITWISE_OR); break;
		case TOKEN_XOR: emitByte(parser, compiler, OP_XOR); break;
		case TOKEN_LSH: emitByte(parser, compiler, OP_LSH); break;
		case TOKEN_RSH: emitByte(parser, compiler, OP_RSH); break;
		case TOKEN_ASH: emitByte(parser, compiler, OP_ASH); break;
		case TOKEN_IS: emitByte(parser, compiler, OP_IS); break;
		case TOKEN_IN: emitByte(parser, compiler, OP_IN); break;
		case TOKEN_IMPLEMENTS: emitByte(parser, compiler, OP_IMPLEMENTS); break;
		default:
			return; // Unreachable.
	}
}

static void grouping(Parser* parser, Compiler* compiler, bool canAssign) {
	expression(parser, compiler);
	consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void expression(Parser* parser, Compiler* compiler) {
	parsePrecedence(parser, compiler, PREC_ASSIGNMENT);
}

static void range(Parser* parser, Compiler* compiler, bool canAssign) {
	expression(parser, compiler);
	emitByte(parser, compiler, OP_RANGE);
}

static void literal(Parser* parser, Compiler* compiler, bool canAssign) {
	switch (parser->previous.type) {
		case TOKEN_FALSE: emitByte(parser, compiler, OP_FALSE); break;
		case TOKEN_NULL: emitByte(parser, compiler, OP_NULL); break;
		case TOKEN_TRUE: emitByte(parser, compiler, OP_TRUE); break;
		default:
			return; // Unreachable.
	}
}

static void replaceEscapes(char at[], char bt[]) {
	for (int j = 0; bt[j]; j++) {
		if (bt[j] == '\\') {
			j++;
			switch (bt[j]) {
				case 'n':
					*at++ = '\n';
					continue;
				case 'a':
					*at++ = '\a';
					continue;
				case 'b':
					*at++ = '\b';
					continue;
				case 'f':
					*at++ = '\f';
					continue;
				case 'r':
					*at++ = '\r';
					continue;
				case 't':
					*at++ = '\t';
					continue;
				case 'v':
					*at++ = '\v';
					continue;
				case '\'':
					*at++ = '\'';
					continue;
				case '\"':
					*at++ = '\"';
					continue;
				case '\\':
					*at++ = '\\';
					continue;
				default:
					break;
			}
		}
		*at++ = bt[j];
	}
	*at = '\0';
}

static void string(Parser* parser, Compiler* compiler, bool canAssign) {
	char* dest = malloc(parser->previous.length - 1);

	memcpy(dest, parser->previous.start + 1, parser->previous.length - 2);
	dest[parser->previous.length - 2] = '\0';

	char* nullString = malloc(parser->previous.length - 1);
	memcpy(nullString, parser->previous.start + 1, parser->previous.length - 2);
	nullString[parser->previous.length - 2] = '\0';

	replaceEscapes(dest, nullString);

	emitConstant(parser, compiler, OBJ_VAL(copyString(parser->vm, dest, parser->previous.length - 2)));

	free(dest);
	free(nullString);
}

static void list(Parser* parser, Compiler* compiler, bool canAssign) {
	uint8_t itemCount = 0;
	if (parser->current.type != TOKEN_RIGHT_SQBR) {
		do {
			expression(parser, compiler);

			if (itemCount == 255) {
				error(parser, "Can't have more than 255 initial items."); //TODO Remove this restiction
			}

			itemCount++;
		} while (match(parser, TOKEN_COMMA));
	}

	consume(parser, TOKEN_RIGHT_SQBR, "Expect ']' after list values.");
	
	emitByte(parser, compiler, OP_LIST);
	emitByte(parser, compiler, itemCount);
}

static void object(Parser* parser, Compiler* compiler, bool canAssign) {

	emitByte(parser, compiler, OP_OBJECT);
	emitByte(parser, compiler, OP_CALL);
	emitByte(parser, compiler, 0);
	
	if (parser->current.type != TOKEN_RIGHT_BRACE) {
		do {
			emitByte(parser, compiler, OP_DUP);
			consume(parser, TOKEN_IDENTIFIER, "Expected identifier key for object key-value pair.");
			
			uint8_t name = identifierConstant(parser, compiler, &parser->previous);

			consume(parser, TOKEN_COLON, "Expected ':' between key-value pair.");

			expression(parser, compiler);

			emitByte(parser, compiler, OP_SET_PROPERTY);
			emitByte(parser, compiler, name);
			emitByte(parser, compiler, OP_POP);
		} while (match(parser, TOKEN_COMMA));
	}

	consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after object body.");
}

static void objectClass(Parser* parser, Compiler* compiler, bool canAssign) {
	emitByte(parser, compiler, OP_CALL);
	emitByte(parser, compiler, 0);

	if (parser->current.type != TOKEN_RIGHT_BRACE) {
		do {
			emitByte(parser, compiler, OP_DUP);
			consume(parser, TOKEN_IDENTIFIER, "Expected identifier key for object key-value pair.");

			uint8_t name = identifierConstant(parser, compiler, &parser->previous);

			consume(parser, TOKEN_COLON, "Expected ':' between key-value pair.");

			expression(parser, compiler);

			emitByte(parser, compiler, OP_SET_PROPERTY);
			emitByte(parser, compiler, name);
			emitByte(parser, compiler, OP_POP);
		} while (match(parser, TOKEN_COMMA));
	}

	consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after object body.");
}

static void this(Parser* parser, Compiler* compiler, bool canAssign) {
	if (parser->currentClass == NULL) {
		error(parser, "Can't use 'this' outside of a class.");
		return;
	}
	variable(parser, compiler, false);
}

static void lambda(Parser* parser, Compiler* c, bool canAssign) {
	Compiler compiler;
	initCompiler(&compiler, c, parser->vm, parser, TYPE_FUNCTION);
	beginScope(&compiler);

	compiler.function->name = copyString(parser->vm, "<lambda>", 8);
	compiler.function->lambda = true;

	bool varArgs = false;

	if (parser->current.type != TOKEN_BIT_OR) {
		do {
			if (varArgs) {
				error(parser, "Variable Arguments must be the last argument in a function definition.");
			}
			compiler.function->arity++;
			if (compiler.function->arity > 255) {
				error(parser, "Can't have more than 255 parameters.");
			}

			uint8_t paramConstant = parseVariable(parser, &compiler, "Expected parameter name.");
			defineVariable(parser, &compiler, paramConstant);
			if (match(parser, TOKEN_ELLIPSIS)) varArgs = true;
		} while (match(parser, TOKEN_COMMA));
	}

	compiler.function->varArgs = varArgs;

	consume(parser, TOKEN_BIT_OR, "Expected '|' after parameters.");

	// The body.
	if (parser->current.type == TOKEN_LEFT_BRACE) {
		consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before lambda body.");
		block(parser, &compiler);
	}
	else {
		expression(parser, &compiler);
		emitByte(parser, &compiler, OP_RETURN);
	}
	

	// Create the function object.
	ObjFunction* function = endCompiler(parser, &compiler);
	emitByte(parser, c, OP_CLOSURE);
	emitByte(parser, c, makeConstant(parser, c, OBJ_VAL(function)));

	for (size_t i = 0; i < function->upvalueCount; i++) {
		emitByte(parser, c, compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(parser, c, compiler.upvalues[i].index);
	}
}

static void lambdaOr(Parser* parser, Compiler* c, bool canAssign) {
	Compiler compiler;
	initCompiler(&compiler, c, parser->vm, parser, TYPE_FUNCTION);
	beginScope(&compiler);

	compiler.function->name = copyString(parser->vm, "<lambda>", 8);
	compiler.function->lambda = true;

	// The body.
	if (parser->current.type == TOKEN_LEFT_BRACE) {
		consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before lambda body.");
		block(parser, &compiler);
	}
	else {
		expression(parser, &compiler);
		emitByte(parser, &compiler, OP_RETURN);
	}


	// Create the function object.
	ObjFunction* function = endCompiler(parser, &compiler);
	emitByte(parser, c, OP_CLOSURE);
	emitByte(parser, c, makeConstant(parser, c, OBJ_VAL(function)));

	for (size_t i = 0; i < function->upvalueCount; i++) {
		emitByte(parser, c, compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(parser, c, compiler.upvalues[i].index);
	}
}

static void and(Parser* parser, Compiler* compiler, bool canAssign) {
	int endJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE_S);

	emitByte(parser, compiler, OP_POP);
	parsePrecedence(parser, compiler, PREC_AND);

	patchJump(parser, compiler, endJump);
}

static void or(Parser* parser, Compiler* compiler, bool canAssign) {
	int elseJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE_S);
	int endJump = emitJump(parser, compiler, OP_JUMP);

	patchJump(parser, compiler, elseJump);
	emitByte(parser, compiler, OP_POP);

	parsePrecedence(parser, compiler, PREC_OR);
	patchJump(parser, compiler, endJump);
}

static void dot(Parser* parser, Compiler* compiler, bool canAssign) {
	consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint8_t name = identifierConstant(parser, compiler, &parser->previous);

	if (canAssign && match(parser, TOKEN_EQUAL)) {
		expression(parser, compiler);
		emitByte(parser, compiler, OP_SET_PROPERTY);
		emitByte(parser, compiler, name);
	}
	else if (canAssign && isAssignment(parser)) {
		TokenType type = parser->previous.type;

		emitByte(parser, compiler, OP_DUP);

		emitByte(parser, compiler, OP_GET_PROPERTY);
		emitByte(parser, compiler, name);

		expression(parser, compiler);
		inplaceOperator(parser, compiler, type);

		emitByte(parser, compiler, OP_SET_PROPERTY);
		emitByte(parser, compiler, name);
	}
	else if (match(parser, TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList(parser, compiler);
		emitByte(parser, compiler, OP_INVOKE);
		emitByte(parser, compiler, name);
		emitByte(parser, compiler, argCount);
	}
	else {
		emitByte(parser, compiler, OP_GET_PROPERTY);
		emitByte(parser, compiler, name);
	}
}

static void ternary(Parser* parser, Compiler* compiler, bool canAssign) {
	int elseJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);
	expression(parser, compiler); // true value
	int trueJump = emitJump(parser, compiler, OP_JUMP);
	patchJump(parser, compiler, elseJump);
	if (match(parser, TOKEN_COLON)) {
		expression(parser, compiler); //false value
	}
	else {
		emitByte(parser, compiler, OP_NULL);
	}
	patchJump(parser, compiler, trueJump);
}

static void super(Parser* parser, Compiler* compiler, bool canAssign) {

	if (parser->currentClass == NULL) {
		error(parser, "Can't use 'super' outside of a class.");
	}
	else if (!parser->currentClass->hasSuperclass) {
		error(parser, "Can't use 'super' in a class with no superclass.");
	}

	if (match(parser, TOKEN_LEFT_PAREN)) {
		uint8_t name = identifierConstant(parser, compiler, &parser->currentClass->superclass);

		namedVariable(parser, compiler, syntheticToken("this"), false);

		uint8_t argCount = argumentList(parser, compiler);
		namedVariable(parser, compiler, syntheticToken("super"), false);
		emitByte(parser, compiler, OP_SUPER_INVOKE);
		emitByte(parser, compiler, name);
		emitByte(parser, compiler, argCount);

		return;
	}

	consume(parser, TOKEN_DOT, "Expect '.' after 'super'.");
	consume(parser, TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint8_t name = identifierConstant(parser, compiler, &parser->previous);

	namedVariable(parser, compiler, syntheticToken("this"), false);

	if (match(parser, TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList(parser, compiler);
		namedVariable(parser, compiler, syntheticToken("super"), false);
		emitByte(parser, compiler, OP_SUPER_INVOKE);
		emitByte(parser, compiler, name);
		emitByte(parser, compiler, argCount);
	}
	else {
		namedVariable(parser, compiler, syntheticToken("super"), false);
		emitByte(parser, compiler, OP_GET_SUPER);
		emitByte(parser, compiler, name);
	}
}

static void call(Parser* parser, Compiler* compiler, bool canAssign) {
	uint8_t argCount = argumentList(parser, compiler);
	emitByte(parser, compiler, OP_CALL);
	emitByte(parser, compiler, argCount);
}

static void index(Parser* parser, Compiler* compiler, bool canAssign) {
	expression(parser, compiler);
	consume(parser, TOKEN_RIGHT_SQBR, "Expected ']' after index.");
	if (canAssign && match(parser, TOKEN_EQUAL)) {
		expression(parser, compiler);
		emitByte(parser, compiler, OP_SET_INDEX);
	}
	else if (canAssign && isAssignment(parser)) {
		TokenType type = parser->previous.type;
		emitByte(parser, compiler, OP_DUP_OFFSET);
		emitByte(parser, compiler, 1);

		emitByte(parser, compiler, OP_DUP_OFFSET);
		emitByte(parser, compiler, 1);

		emitByte(parser, compiler, OP_GET_INDEX);

		expression(parser, compiler);
		inplaceOperator(parser, compiler, type);
		emitByte(parser, compiler, OP_SET_INDEX);
	}
	else {
		emitByte(parser, compiler, OP_GET_INDEX);
	}
}

static uint8_t argumentList(Parser* parser, Compiler* compiler) {
	uint8_t argCount = 0;
	if (parser->current.type != TOKEN_RIGHT_PAREN) {
		do {
			expression(parser, compiler);

			if (argCount == 255) {
				error(parser, "Can't have more than 255 arguments.");
			}

			argCount++;
		} while (match(parser, TOKEN_COMMA));
	}

	consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static int resolveLocal(Parser* parser, Compiler* compiler, Token* name) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error(parser, "Can't read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

static int addUpvalue(Parser* parser, Compiler* compiler, uint8_t index, bool isLocal) {
	int upvalueCount = compiler->function->upvalueCount;

	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_MAX + 1) {
		error(parser, "Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Parser* parser, Compiler* compiler, Token* name) {
	if (compiler->enclosing == NULL) return -1;

	int local = resolveLocal(parser, compiler->enclosing, name);
	if (local != -1) {
		compiler->enclosing->locals[local].isCaptured = true;
		return addUpvalue(parser, compiler, (uint8_t)local, true);
	}

	int upvalue = resolveUpvalue(parser, compiler->enclosing, name);
	if (upvalue != -1) {
		return addUpvalue(parser, compiler, (uint8_t)upvalue, false);
	}

	return -1;
}

static void namedVariable(Parser* parser, Compiler* compiler, Token name, bool canAssign) {

	uint8_t getOp, setOp;
	int arg = resolveLocal(parser, compiler, &name);
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else if ((arg = resolveUpvalue(parser, compiler, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	else {
		arg = identifierConstant(parser, compiler, &name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(parser, TOKEN_EQUAL)) {
		expression(parser, compiler);
		emitByte(parser, compiler, setOp);
	}
	else if (canAssign && isAssignment(parser)) {
		TokenType type = parser->previous.type;
		emitByte(parser, compiler, getOp);
		emitByte(parser, compiler, arg);

		expression(parser, compiler);
		inplaceOperator(parser, compiler, type);

		emitByte(parser, compiler, setOp);
	}
	else {
		emitByte(parser, compiler, getOp);
	}
	emitByte(parser, compiler, arg);
}

static void variable(Parser* parser, Compiler* compiler, bool canAssign) {
	namedVariable(parser, compiler, parser->previous, canAssign);
}

static void expressionStatement(Parser* parser, Compiler* compiler) {
	expression(parser, compiler);
	consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(parser, compiler, OP_POP);
}

static void block(Parser* parser, Compiler* compiler) {
	while (parser->current.type != TOKEN_RIGHT_BRACE && parser->current.type != TOKEN_EOF) {
		declaration(parser, compiler);
	}

	consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void ifStatement(Parser* parser, Compiler* compiler) {
	consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression(parser, compiler);
	consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	size_t thenJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);
	statement(parser, compiler);

	int elseJump = emitJump(parser, compiler, OP_JUMP);

	patchJump(parser, compiler, thenJump);

	if (match(parser, TOKEN_ELSE)) statement(parser, compiler);

	patchJump(parser, compiler, elseJump);
}

static void whileStatement(Parser* parser, Compiler* compiler) {

	bool wasLoop = compiler->isLoop;
	int prevBreakPoint = compiler->breakPoint;
	int prevContinuePoint = compiler->continuePoint;

	compiler->isLoop = true;
	int loopStart = currentChunk(compiler)->count;

	compiler->continuePoint = loopStart;
	consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression(parser, compiler);
	consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int exitJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);
	compiler->breakPoint = exitJump;
	statement(parser, compiler);

	emitLoop(parser, compiler, loopStart);

	patchJump(parser, compiler, exitJump);
	compiler->isLoop = wasLoop;
	compiler->breakPoint = prevBreakPoint;
	compiler->continuePoint = prevContinuePoint;
}

static void forStatement(Parser* parser, Compiler* compiler) {

	bool wasLoop = compiler->isLoop;
	int prevBreakPoint = compiler->breakPoint;
	int prevContinuePoint = compiler->continuePoint;

	compiler->isLoop = true;
	beginScope(compiler);

	consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

	if (match(parser, TOKEN_SEMICOLON)) {
		// No initializer.
	}
	else if (match(parser, TOKEN_VAR)) {
		varDeclaration(parser, compiler);
	}
	else {
		expressionStatement(parser, compiler);
	}

	int loopStart = currentChunk(compiler)->count;
	compiler->continuePoint = loopStart;
	int exitJump = -1;
	if (!match(parser, TOKEN_SEMICOLON)) {
		expression(parser, compiler);
		consume(parser, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);
		compiler->breakPoint = exitJump;
	}
	else {
		emitByte(parser, compiler, OP_TRUE);
		exitJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);
		compiler->breakPoint = exitJump;
	}

	if (!match(parser, TOKEN_RIGHT_PAREN)) {
		int bodyJump = emitJump(parser, compiler, OP_JUMP);

		int incrementStart = currentChunk(compiler)->count;
		expression(parser, compiler);
		emitByte(parser, compiler, OP_POP);
		consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for statement.");

		emitLoop(parser, compiler, loopStart);
		loopStart = incrementStart;
		compiler->continuePoint = incrementStart;
		patchJump(parser, compiler, bodyJump);
	}

	statement(parser, compiler);

	emitLoop(parser, compiler, loopStart);

	if (exitJump != -1) {
		patchJump(parser, compiler, exitJump);
	}

	endScope(parser, compiler);
	compiler->isLoop = wasLoop;
	compiler->breakPoint = prevBreakPoint;
	compiler->continuePoint = prevContinuePoint;
}

static void foreachStatement(Parser* parser, Compiler* compiler) {

	beginScope(compiler);

	consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'foreach'.");

	consume(parser, TOKEN_VAR, "Expected 'var' in foreach clause.");

	uint8_t global = parseVariable(parser, compiler, "Expect variable name.");

	defineVariable(parser, compiler, global);

	Token item = parser->previous;

	emitByte(parser, compiler, OP_NULL);

	emitByte(parser, compiler, OP_SET_LOCAL);
	emitByte(parser, compiler, resolveLocal(parser, compiler, &item));

	consume(parser, TOKEN_IN, "Expected 'in' after variable in foreach clause.");

	expression(parser, compiler);

	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after foreach clause.");

	Token iteratorToken = syntheticToken("iterator");

	uint8_t iterator = identifierConstant(parser, compiler, &iteratorToken);

	emitByte(parser, compiler, OP_INVOKE);
	emitByte(parser, compiler, iterator);
	emitByte(parser, compiler, 0);

	int loopStart = currentChunk(compiler)->count;

	emitByte(parser, compiler, OP_DUP);

	Token doneToken = syntheticToken("done");

	uint8_t done = identifierConstant(parser, compiler, &doneToken);

	emitByte(parser, compiler, OP_INVOKE);
	emitByte(parser, compiler, done);
	emitByte(parser, compiler, 0);

	emitByte(parser, compiler, OP_NOT);
	int exitJump = emitJump(parser, compiler, OP_JUMP_IF_FALSE);

	emitByte(parser, compiler, OP_DUP);

	Token nextToken = syntheticToken("next");

	uint8_t next = identifierConstant(parser, compiler, &nextToken);

	emitByte(parser, compiler, OP_INVOKE);
	emitByte(parser, compiler, next);
	emitByte(parser, compiler, 0);

	emitByte(parser, compiler, OP_SET_LOCAL);
	emitByte(parser, compiler, resolveLocal(parser, compiler, &item));

	emitByte(parser, compiler, OP_POP);

	statement(parser, compiler);

	emitLoop(parser, compiler, loopStart);

	patchJump(parser, compiler, exitJump);

	endScope(parser, compiler);
}

static void returnStatement(Parser* parser, Compiler* compiler) {

	if (compiler->type == TYPE_SCRIPT) {
		error(parser, "Can't return from top-level code.");
	}

	if (match(parser, TOKEN_SEMICOLON)) {
		emitReturn(parser, compiler);
	}
	else {
		if (compiler->type == TYPE_INITIALIZER) {
			error(parser, "Can't return a value from an initializer.");
		}

		expression(parser, compiler);
		consume(parser, TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(parser, compiler, OP_RETURN);
	}
}

static void exportStatement(Parser* parser, Compiler* compiler) {
	expression(parser, compiler);
	consume(parser, TOKEN_AS, "Expected 'as' between export value and name.");
	consume(parser, TOKEN_IDENTIFIER, "Expected export name.");
	uint8_t name = identifierConstant(parser, compiler, &parser->previous);
	emitByte(parser, compiler, OP_EXPORT);
	emitByte(parser, compiler, name);

	consume(parser, TOKEN_SEMICOLON, "Expect ';' after export statement.");
}

static void beginScope(Compiler* compiler) {
	compiler->scopeDepth++;
}

static void endScope(Parser* parser, Compiler* compiler) {
	compiler->scopeDepth--;
	while (compiler->localCount > 0 && compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
		if (compiler->locals[compiler->localCount - 1].isCaptured) {
			emitByte(parser, compiler, OP_CLOSE_UPVALUE);
		}
		else {
			emitByte(parser, compiler, OP_POP);
		}
		compiler->localCount--;
	}
}

static void statement(Parser* parser, Compiler* compiler) {
	if (match(parser, TOKEN_IF)) {
		ifStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_WHILE)) {
		whileStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_FOR)) {
		forStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_FOREACH)) {
		foreachStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_RETURN)) {
		returnStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_LEFT_BRACE)) {
		beginScope(compiler);
		block(parser, compiler);
		endScope(parser, compiler);
	}
	else if (match(parser, TOKEN_EXPORT)) {
		exportStatement(parser, compiler);
	}
	else if (match(parser, TOKEN_CONTINUE)) {
		if (!compiler->isLoop) error(parser, "Cannot use 'continue' outside of a loop.");
		emitLoop(parser, compiler, compiler->continuePoint);
		consume(parser, TOKEN_SEMICOLON, "Expect ';' after continue.");
	}
	else if (match(parser, TOKEN_BREAK)) {
		if (!compiler->isLoop) error(parser, "Cannot use 'break' outside of a loop.");

		emitByte(parser, compiler, OP_FALSE);
		emitLoop(parser, compiler, compiler->breakPoint - 1);

		consume(parser, TOKEN_SEMICOLON, "Expect ';' after break.");
	}
	else {
		expressionStatement(parser, compiler);
	}
}

static void synchronize(Parser* parser) {
	parser->panicMode = false;

	while (parser->current.type != TOKEN_EOF) {
		if (parser->previous.type == TOKEN_SEMICOLON) return;

		switch (parser->current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUNCTION:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_RETURN:
				return;

			default:
				// Do nothing.
				;
		}

		advance(parser);
	}
}

static uint8_t identifierConstant(Parser* parser, Compiler* compiler, Token* name) {
	return makeConstant(parser, compiler, OBJ_VAL(copyString(parser->vm, name->start, name->length)));
}

static void addLocal(Parser* parser, Compiler* compiler, Token name) {
	if (compiler->localCount == UINT8_MAX + 1) {
		error(parser, "Too many local variables in function.");
		return;
	}

	Local* local = &compiler->locals[compiler->localCount++];
	local->name = name;
	local->depth = -1;
	local->isCaptured = false;
}

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariable(Parser* parser, Compiler* compiler) {
	if (compiler->scopeDepth == 0) return;

	Token* name = &parser->previous;

	for (int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (local->depth != -1 && local->depth < compiler->scopeDepth) {
			break;
		}

		if (identifiersEqual(name, &local->name)) {
			error(parser, "Already variable with this name in this scope.");
		}
	}

	addLocal(parser, compiler, *name);
}

static void markInitialized(Compiler* compiler) {
	if (compiler->scopeDepth == 0) return;
	compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
}

static void defineVariable(Parser* parser, Compiler* compiler, uint8_t global) {
	if (compiler->scopeDepth > 0) {
		markInitialized(compiler);
		return;
	}

	emitByte(parser, compiler, OP_DEFINE_GLOBAL);
	emitByte(parser, compiler, global);
}

static uint8_t parseVariable(Parser* parser, Compiler* compiler, const char* errorMessage) {
	consume(parser, TOKEN_IDENTIFIER, errorMessage);

	declareVariable(parser, compiler);
	if (compiler->scopeDepth > 0) return 0;

	return identifierConstant(parser, compiler, &parser->previous);
}

static void varDeclaration(Parser* parser, Compiler* compiler) {
	uint8_t global = parseVariable(parser, compiler, "Expect variable name.");

	if (match(parser, TOKEN_EQUAL)) {
		expression(parser, compiler);
	}
	else {
		emitByte(parser, compiler, OP_NULL);
	}
	consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(parser, compiler, global);
}

static void function(Parser* parser, Compiler* c, FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, c, parser->vm, parser, type);
	beginScope(&compiler);

	// Compile the parameter list.
	consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	bool varArgs = false;

	if (parser->current.type != TOKEN_RIGHT_PAREN) {
		do {
			if (varArgs) {
				error(parser, "Variable Arguments must be the last argument in a function definition.");
			}
			compiler.function->arity++;
			if (compiler.function->arity > 255) {
				error(parser, "Can't have more than 255 parameters.");
			}

			uint8_t paramConstant = parseVariable(parser, &compiler, "Expect parameter name.");
			defineVariable(parser, &compiler, paramConstant);
			if (match(parser, TOKEN_ELLIPSIS)) varArgs = true;
		} while (match(parser, TOKEN_COMMA));
	}
	compiler.function->varArgs = varArgs;
	consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

	// The body.
	consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block(parser, &compiler);

	// Create the function object.
	ObjFunction* function = endCompiler(parser, &compiler);
	emitByte(parser, c, OP_CLOSURE);
	emitByte(parser, c, makeConstant(parser, c, OBJ_VAL(function)));

	for (size_t i = 0; i < function->upvalueCount; i++) {
		emitByte(parser, c, compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(parser, c, compiler.upvalues[i].index);
	}
}

static void funDeclaration(Parser* parser, Compiler* compiler) {
	uint8_t global = parseVariable(parser, compiler, "Expect function name.");
	markInitialized(compiler);
	function(parser, compiler, TYPE_FUNCTION);
	defineVariable(parser, compiler, global);
}

static void method(Parser* parser, Compiler* compiler) {
	consume(parser, TOKEN_IDENTIFIER, "Expect method name.");

	uint8_t constant = 0;

	if (parser->previous.length == 8 && memcmp(parser->previous.start, "operator", 8) == 0) {
		advance(parser);
		constant = identifierConstant(parser, compiler, &parser->previous);
	}
	else {
		constant = identifierConstant(parser, compiler, &parser->previous);
	}

	FunctionType type = TYPE_METHOD;

	if (parser->previous.length == parser->currentClass->name.length && memcmp(parser->previous.start, parser->currentClass->name.start, parser->previous.length) == 0) {
		type = TYPE_INITIALIZER;
	}

	function(parser, compiler, type);

	emitByte(parser, compiler, OP_METHOD);
	emitByte(parser, compiler, constant);
}

static void classDeclaration(Parser* parser, Compiler* compiler) {
	consume(parser, TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser->previous;
	uint8_t nameConstant = identifierConstant(parser, compiler, &parser->previous);
	declareVariable(parser, compiler);

	emitByte(parser, compiler, OP_CLASS);
	emitByte(parser, compiler, nameConstant);
	defineVariable(parser, compiler, nameConstant);

	ClassCompiler classCompiler;
	classCompiler.name = parser->previous;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = parser->currentClass;
	parser->currentClass = &classCompiler;

	if (match(parser, TOKEN_EXTENDS)) {
		consume(parser, TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(parser, compiler, false);

		if (identifiersEqual(&className, &parser->previous)) {
			error(parser, "A class can't inherit from itself.");
		}

		classCompiler.superclass = parser->previous;

		beginScope(compiler);
		addLocal(parser, compiler, syntheticToken("super"));
		defineVariable(parser, compiler, 0);

		namedVariable(parser, compiler, className, false);
		emitByte(parser, compiler, OP_INHERIT);
		classCompiler.hasSuperclass = true;
	}
	else {
		classCompiler.superclass = syntheticToken("<object>");

		emitByte(parser, compiler, OP_OBJECT);

		beginScope(compiler);
		addLocal(parser, compiler, syntheticToken("super"));
		defineVariable(parser, compiler, 0);

		namedVariable(parser, compiler, className, false);
		emitByte(parser, compiler, OP_INHERIT);
		classCompiler.hasSuperclass = true;
	}

	namedVariable(parser, compiler, className, false);

	consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

	while (parser->current.type != TOKEN_RIGHT_BRACE && parser->current.type != TOKEN_EOF) {
		method(parser, compiler);
	}

	consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(parser, compiler, OP_POP);

	if (classCompiler.hasSuperclass) {
		endScope(parser, compiler);
	}

	parser->currentClass = parser->currentClass->enclosing;
}

static void importDeclaration(Parser* parser, Compiler* compiler) {

	consume(parser, TOKEN_IDENTIFIER, "Expected import name.");

	size_t length = 1;
	char* path = malloc(parser->previous.length + 1);
	memcpy(path, parser->previous.start, parser->previous.length);
	length += parser->previous.length;

	Token filename = parser->previous;

	bool single = true;

	while (match(parser, TOKEN_DOT)) {
		single = false;
		consume(parser, TOKEN_IDENTIFIER, "Expected import name.");
		filename = parser->previous;
		size_t oldSize = length;
		length += parser->previous.length;
		path = realloc(path, oldSize + length + 1);
		path[oldSize-1] = '/';
		memcpy(path + oldSize, parser->previous.start, parser->previous.length);
	}

	length -= single;

	path[length] = '\0';

	uint8_t varName;

	uint8_t pathConstant = makeConstant(parser, compiler, OBJ_VAL(copyString(compiler->vm, path, length)));

	uint8_t fileNameConstant = makeConstant(parser, compiler, OBJ_VAL(copyString(compiler->vm, filename.start, filename.length)));

	if (match(parser, TOKEN_AS)) {
		consume(parser, TOKEN_IDENTIFIER, "Expected import alias.");
		varName = identifierConstant(parser, compiler, &parser->previous);
	}
	else {
		varName = identifierConstant(parser, compiler, &parser->previous);
	}
	declareVariable(parser, compiler);

	emitByte(parser, compiler, OP_IMPORT);
	emitByte(parser, compiler, pathConstant);
	emitByte(parser, compiler, fileNameConstant);

	defineVariable(parser, compiler, varName);


	consume(parser, TOKEN_SEMICOLON, "Expected ';' after import.");
}

static void fromDeclaration(Parser* parser, Compiler* compiler) {
	consume(parser, TOKEN_IDENTIFIER, "Expected import name.");

	size_t length = 1;
	char* path = malloc(parser->previous.length + 1);
	memcpy(path, parser->previous.start, parser->previous.length);
	length += parser->previous.length;

	Token filename = parser->previous;

	bool single = true;

	while (match(parser, TOKEN_DOT)) {
		single = false;
		consume(parser, TOKEN_IDENTIFIER, "Expected import name.");
		filename = parser->previous;
		size_t oldSize = length;
		length += parser->previous.length;
		path = realloc(path, oldSize + length + 1);
		path[oldSize - 1] = '/';
		memcpy(path + oldSize, parser->previous.start, parser->previous.length);
	}

	length -= single;

	path[length] = '\0';

	uint8_t pathConstant = makeConstant(parser, compiler, OBJ_VAL(copyString(compiler->vm, path, length)));

	uint8_t fileNameConstant = makeConstant(parser, compiler, OBJ_VAL(copyString(compiler->vm, filename.start, filename.length)));

	consume(parser, TOKEN_IMPORT, "Expected 'import' after import path.");

	emitByte(parser, compiler, OP_IMPORT);
	emitByte(parser, compiler, pathConstant);
	emitByte(parser, compiler, fileNameConstant);

	do {
		uint8_t name = parseVariable(parser, compiler, "Expected export name.");

		emitByte(parser, compiler, OP_DUP);
		emitByte(parser, compiler, OP_GET_PROPERTY);
		emitByte(parser, compiler, name);
		defineVariable(parser, compiler, name);
	} while (match(parser, TOKEN_COMMA));
	emitByte(parser, compiler, OP_POP);

	consume(parser, TOKEN_SEMICOLON, "Expected ';' after import.");
}

static void declaration(Parser* parser, Compiler* compiler) {
	if (match(parser, TOKEN_CLASS)) {
		classDeclaration(parser, compiler);
	}
	else if (match(parser, TOKEN_FUNCTION)) {
		funDeclaration(parser, compiler);
	}
	else if (match(parser, TOKEN_VAR)) {
		varDeclaration(parser, compiler);
	}
	else if (match(parser, TOKEN_IMPORT)) {
		importDeclaration(parser, compiler);
	}
	else if (match(parser, TOKEN_FROM)) {
		fromDeclaration(parser, compiler);
	}
	else {
		statement(parser, compiler);
	}

	if (parser->panicMode) synchronize(parser);
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
  [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
  [TOKEN_LEFT_BRACE] = {object, objectClass, PREC_PRIMARY},
  [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
  [TOKEN_LEFT_SQBR] = {list, index, PREC_CALL},
  [TOKEN_RIGHT_SQBR] = {NULL, NULL, PREC_NONE},
  [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
  [TOKEN_DOT] = {NULL, dot, PREC_CALL},
  [TOKEN_D_ELLIPSIS] = {NULL, range, PREC_RANGE},
  [TOKEN_MINUS] = {unary,  binary, PREC_TERM},
  [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
  [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
  [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
  [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
  [TOKEN_BANG] = {unary, NULL, PREC_NONE},
  [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
  [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
  [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
  [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
  [TOKEN_STRING] = {string, NULL, PREC_NONE},
  [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
  [TOKEN_AND] = {NULL, and, PREC_AND},
  [TOKEN_BIT_AND] = {NULL, binary, PREC_BIT_AND},
  [TOKEN_OR] = {lambdaOr, or, PREC_OR},
  [TOKEN_BIT_OR] = {lambda, binary, PREC_BIT_OR},
  [TOKEN_BIT_NOT] = {unary, NULL, PREC_NONE},
  [TOKEN_XOR] = {NULL, binary, PREC_XOR},
  [TOKEN_LSH] = {NULL, binary, PREC_SHIFT},
  [TOKEN_RSH] = {NULL, binary, PREC_SHIFT},
  [TOKEN_ASH] = {NULL, binary, PREC_SHIFT},
  [TOKEN_QUESTION] = {NULL, ternary, PREC_TERNARY},
  [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},
  [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
  [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
  [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
  [TOKEN_EXTENDS] = {NULL, NULL, PREC_NONE},
  [TOKEN_EXPORT] = {NULL, NULL, PREC_NONE},
  [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
  [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
  [TOKEN_FOREACH] = {NULL, NULL, PREC_NONE},
  [TOKEN_FUNCTION] = {NULL, NULL, PREC_NONE},
  [TOKEN_IF] = {NULL, NULL, PREC_NONE},
  [TOKEN_IS] = {NULL, binary, PREC_EQUALITY},
  [TOKEN_IN] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
  [TOKEN_IMPLEMENTS] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_NULL] = {literal, NULL, PREC_NONE},
  [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
  [TOKEN_SUPER] = {super, NULL, PREC_NONE},
  [TOKEN_THIS] = {this, NULL, PREC_NONE},
  [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
  [TOKEN_TYPEOF] = {unary, NULL, PREC_NONE},
  [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
  [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
  [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
  [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

void markCompilerRoots(Compiler* compiler) {
	while (compiler != NULL) {
		markObject(compiler->vm, (Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}

ObjFunction* compile(VM* vm, const char* source, Chunk* chunk) {
	Scanner scanner;
	initScanner(&scanner, source);

	Parser parser;
	parser.vm = vm;
	parser.hadError = false;
	parser.panicMode = false;
	parser.scanner = &scanner;
	parser.currentClass = NULL;

	Compiler compiler;
	initCompiler(&compiler, NULL, vm, &parser, TYPE_SCRIPT);

	advance(&parser);

	while (!match(&parser, TOKEN_EOF)) {
		declaration(&parser, &compiler);
	}

	ObjFunction* function = endCompiler(&parser, &compiler);
	return parser.hadError ? NULL : function;
}