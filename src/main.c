#define _CRT_SECURE_NO_WARNINGS
#include <vm/chunk.h>
#include <vm/opcodes.h>
#include <debug/disassemble.h>
#include <vm/vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

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

	while (replKeepRunning) {
		printf(">>> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		interpretVM(&vm, line);

		/*Value lineVal = *vm.stackTop;

		if (!IS_NULL(lineVal)) {
			char* string = valueToString(lineVal);
			printf("%s\n", string);
			free(string);
		}*/
	}
	freeVM(&vm);
}

static char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(-4);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(-4);
	}


	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytesRead] = '\0';

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(-4);
	}

	fclose(file);
	return buffer;
}

static void runFile(const char* path) {
	char* source = readFile(path);
	InterpreterResult result = interpret(source);
	free(source);

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
