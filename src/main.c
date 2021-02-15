#define _CRT_SECURE_NO_WARNINGS
#include <vm/chunk.h>
#include <vm/opcodes.h>
#include <debug/disassemble.h>
#include <vm/vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <core/file.h>

// The below variable and function allows the user to exit with Ctrl-C
static volatile sig_atomic_t replKeepRunning = 1;

static void replSigHandler(int _) {
	(void)_;
	replKeepRunning = 0;
}

static void repl() {
	// Register signal
	signal(SIGINT, replSigHandler);
	char line[1024]; // Max length, TODO

	VM vm;
	initVM(&vm);

	char* scriptName = malloc(9);
	strcpy(scriptName, "<script>");

	while (replKeepRunning) {
		printf(">>> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}



		interpretVM(&vm, ".", line, scriptName);

		/*Value lineVal = *vm.stackTop;

		if (!IS_NULL(lineVal)) {
			char* string = valueToString(lineVal);
			printf("%s\n", string);
			free(string);
		}*/
	}
	freeVM(&vm);
}

static char* fromLastInstance(const char* haystack, const char* needle) {
	if (*needle == '\0')
		return (char*)haystack;

	char* result = NULL;
	for (;;) {
		char* p = strstr(haystack, needle);
		if (p == NULL)
			break;
		result = p;
		haystack = p + 1;
	}

	return result;
}

static void runFile(const char* path) {
	int length = strlen(path);
	char* slashRoot = malloc(length + 1);
	strcpy(slashRoot, path);

	char currentChar; int i;
	for (char currentChar = *slashRoot, i = 0; currentChar != '\0'; i++) {
		currentChar = slashRoot[i];
		if (currentChar == '\\') {
			slashRoot[i] = '/';
		}
	}

	int index = fromLastInstance(slashRoot, "/") - slashRoot;

	char* base = malloc(index + 2);
	memcpy(base, slashRoot, index);
	base[index] = '/';
	base[index + 1] = '\0';

	char* source = readFile(path);
	if (source == NULL) exit(-4);

	char* name = malloc(strlen(fromLastInstance(slashRoot, "/")));
	strcpy(name, fromLastInstance(slashRoot, "/") + 1);

	InterpreterResult result = interpret(base, name, source);
	free(source);
	free(base);
	free(slashRoot);

	if (result == STATUS_COMPILE_ERR) exit(-2);
	if (result == STATUS_RUNTIME_ERR) exit(-3);
}

int main(int argc, const char** argv) {

	if (argc == 1) {
		repl();
	}
	else if (argc == 2) {
		runFile(argv[1]);
	}
	else {
		fprintf(stderr, "Usage: fox [filepath]\n");
		return -1;
	}

	return 0;
}
