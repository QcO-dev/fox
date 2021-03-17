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

#if defined(_WIN32) || defined(_WIN64) || defined(WINDOWS)
#include <direct.h>
#define getCurrentDir _getcwd
#else
#include <unistd.h>
#define getCurrentDir getcwd
#endif

// The below variable and function allows the user to exit with Ctrl-C
static volatile sig_atomic_t replKeepRunning = 1;

static void replSigHandler(int _) {
	(void)_;
	replKeepRunning = 0;
}

static void repl() {
	// Register signal
	signal(SIGINT, replSigHandler);

	VM vm;
	initVM(&vm);

	char* scriptName = malloc(9);
	strcpy(scriptName, "<script>");

	while (replKeepRunning) {
		printf(">>> ");

		char* line = inputString(stdin, 30);

		interpretVM(&vm, ".", scriptName, line);
		free(line);
	}
	freeVM(&vm);
}

static void runFile(const char* path) {
	int length = strlen(path);
	char* slashRoot = malloc(length + 1);
	strcpy(slashRoot, path);

	char currentChar; int i;
	for (currentChar = *slashRoot, i = 0; currentChar != '\0'; i++) {
		currentChar = slashRoot[i];
		if (currentChar == '\\') {
			slashRoot[i] = '/';
		}
	}

	char* lastInstance = fromLastInstance(slashRoot, "/");

	char* base;
	char* name;

	if (lastInstance != NULL) {
		int index = lastInstance - slashRoot;

		base = malloc(index + 2);
		memcpy(base, slashRoot, index);
		base[index] = '/';
		base[index + 1] = '\0';

		name = malloc(strlen(lastInstance));
		strcpy(name, lastInstance + 1);
	}
	else {
		char buffer[FILENAME_MAX];
		getCurrentDir(buffer, FILENAME_MAX);

		base = malloc(strlen(buffer) + 2);
		strcpy(base, buffer);
		base[strlen(buffer)] = '/';
		base[strlen(buffer) + 1] = '\0';

		name = malloc(strlen(slashRoot) + 1);
		strcpy(name, slashRoot);
	}

	char* source = readFile(path);
	if (source == NULL) {
		fprintf(stderr, "\n");
		exit(-4);
	}

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
