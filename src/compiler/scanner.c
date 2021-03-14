#include "scanner.h"
#include <string.h>

void initScanner(Scanner* scanner, const char* source) {
	scanner->start = source;
	scanner->current = source;
	scanner->line = 1;
}

static bool isAtEnd(Scanner* scanner) {
	return *scanner->current == '\0';
}

static Token makeToken(Scanner* scanner, TokenType type) {
	Token token;
	token.type = type;
	token.start = scanner->start;
	token.length = (int)(scanner->current - scanner->start);
	token.line = scanner->line;

	return token;
}

static char advance(Scanner* scanner) {
	scanner->current++;
	return scanner->current[-1];
}

static bool match(Scanner* scanner, char expected) {
	if (isAtEnd(scanner)) return false;
	if (*scanner->current != expected) return false;

	scanner->current++;
	return true;
}

static char peekNext(Scanner* scanner) {
	if (isAtEnd(scanner)) return '\0';
	return scanner->current[1];
}

static void skipWhitespace(Scanner* scanner) {
	for (;;) {
		char c = *scanner->current;
		switch (c) {
			case ' ':
			case '\r':
			case '\t':
				advance(scanner);
				break;
			case '\n':
				scanner->line++;
				advance(scanner);
				break;

			case '/':
				if (peekNext(scanner) == '/') {
					// A comment goes until the end of the line.
					while (*scanner->current != '\n' && !isAtEnd(scanner)) advance(scanner);
				}
				else {
					return;
				}
				break;

			default:
				return;
		}
	}
}

static Token errorToken(Scanner* scanner, const char* message) {
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner->line;

	return token;
}

static Token string(Scanner* scanner) {
	bool escape = false;
	while (!isAtEnd(scanner)) {

		if (*scanner->current == '"' && !escape) break;

		if (*scanner->current == '\n') scanner->line++;
		if (escape) escape = false;
		if (*scanner->current == '\\') escape = true;
		advance(scanner);
	}

	if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

	// The closing quote.
	advance(scanner);
	return makeToken(scanner, TOKEN_STRING);
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static Token number(Scanner* scanner) {
	while (isDigit(*scanner->current)) advance(scanner);

	// Look for a fractional part.
	if (*scanner->current == '.' && isDigit(peekNext(scanner))) {
		// Consume the ".".
		advance(scanner);

		while (isDigit(*scanner->current)) advance(scanner);
	}

	return makeToken(scanner, TOKEN_NUMBER);
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_';
}

static TokenType checkKeyword(Scanner* scanner, int start, int length, const char* rest, TokenType type) {
	if (scanner->current - scanner->start == start + length && memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner* scanner) {
	switch (*scanner->start) {
		case 'c': 
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
					case 'o': return checkKeyword(scanner, 2, 6, "ntinue", TOKEN_CONTINUE);
				}
			}
			break;
		case 'a': return checkKeyword(scanner, 1, 1, "s", TOKEN_AS);
		case 'b': return checkKeyword(scanner, 1, 4, "reak", TOKEN_BREAK);
		case 'e':
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
					case 'x': {
						switch (scanner->start[2]) {
							case 't': return checkKeyword(scanner, 3, 4, "ends", TOKEN_EXTENDS);
							case 'p': return checkKeyword(scanner, 3, 3, "ort", TOKEN_EXPORT);
						}
					}
				}
			}
			break;

		case 'i': if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
				case 'f': return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
				case 's': return checkKeyword(scanner, 2, 0, "", TOKEN_IS);
				case 'n': return checkKeyword(scanner, 2, 0, "", TOKEN_IN);
				case 'm': return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
			}
			break;
		}
		case 'n': return checkKeyword(scanner, 1, 3, "ull", TOKEN_NULL);
		case 'r': return checkKeyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
		case 's': return checkKeyword(scanner, 1, 4, "uper", TOKEN_SUPER);
		case 'v': return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
		case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);

		case 'f':
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'a': return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
					case 'o': return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
					case 'u': return checkKeyword(scanner, 2, 6, "nction", TOKEN_FUNCTION);
					case 'r': return checkKeyword(scanner, 2, 2, "om", TOKEN_FROM);
				}
			}
			break;
		case 't':
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'h': return checkKeyword(scanner, 2, 2, "is", TOKEN_THIS);
					case 'r': return checkKeyword(scanner, 2, 2, "ue", TOKEN_TRUE);
				}
			}
			break;
	}

	return TOKEN_IDENTIFIER;
}

static Token identifier(Scanner* scanner) {
	while (isAlpha(*scanner->current) || isDigit(*scanner->current)) advance(scanner);

	return makeToken(scanner, identifierType(scanner));
}

Token scanToken(Scanner* scanner) {

	skipWhitespace(scanner);

	scanner->start = scanner->current;

	if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

	char c = advance(scanner);
	if (isAlpha(c)) return identifier(scanner);
	if (isDigit(c)) return number(scanner);

	switch (c) {
		case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
		case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
		case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
		case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
		case '[': return makeToken(scanner, TOKEN_LEFT_SQBR);
		case ']': return makeToken(scanner, TOKEN_RIGHT_SQBR);
		case ';': return makeToken(scanner, TOKEN_SEMICOLON);
		case ':': return makeToken(scanner, TOKEN_COLON);
		case ',': return makeToken(scanner, TOKEN_COMMA);
		case '-': return makeToken(scanner, TOKEN_MINUS);
		case '+': return makeToken(scanner, TOKEN_PLUS);
		case '/': return makeToken(scanner, TOKEN_SLASH);
		case '*': return makeToken(scanner, TOKEN_STAR);
		case '~': return makeToken(scanner, TOKEN_BIT_NOT);
		case '^': return makeToken(scanner, TOKEN_XOR);
		case '?': return makeToken(scanner, TOKEN_QUESTION);

		case '.': return makeToken(scanner,
			match(scanner, '.') ? TOKEN_D_ELLIPSIS : TOKEN_DOT);

		case '!':
			return makeToken(scanner,
				match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
		case '=':
			return makeToken(scanner,
				match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
		case '<': {
			TokenType type = TOKEN_LESS;

			if (match(scanner, '<')) {
				type = TOKEN_LSH;
			}
			else if (match(scanner, '=')) {
				type = TOKEN_LESS_EQUAL;
			}

			return makeToken(scanner, type);
		}
		case '>': {
			TokenType type = TOKEN_GREATER;

			if (match(scanner, '>')) {
				type = TOKEN_RSH;
				if (match(scanner, '>')) {
					type = TOKEN_ASH;
				}
			}
			else if (match(scanner, '=')) {
				type = TOKEN_GREATER_EQUAL;
			}

			return makeToken(scanner, type);
		}
		case '&':
			return makeToken(scanner,
				match(scanner, '&') ? TOKEN_AND : TOKEN_BIT_AND);
		case '|':
			return makeToken(scanner,
				match(scanner, '|') ? TOKEN_OR : TOKEN_BIT_OR);

		case '"': return string(scanner);
	}


	return errorToken(scanner, "Unexpected character.");
}
