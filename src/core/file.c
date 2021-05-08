#include "file.h"
#include <core/common.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

File readFile(const char* path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		size_t length = snprintf(NULL, 0, "Could not open file \"%s\".", path);

		char* error = malloc(length + 1);
		sprintf(error, "Could not open file \"%s\".", path);

		return (File) {.contents = error, .isError = true};
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		size_t length = snprintf(NULL, 0, "Not enough memory to read \"%s\".", path);

		char* error = malloc(length + 1);
		sprintf(error, "Not enough memory to read \"%s\".", path);

		return (File) { .contents = error, .isError = true };
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytesRead] = '\0';

	if (bytesRead < fileSize) {
		size_t length = snprintf(NULL, 0, "Could not read file \"%s\".", path);

		char* error = malloc(length + 1);
		sprintf(error, "Could not read file \"%s\".", path);

		return (File) { .contents = error, .isError = true };
	}

	fclose(file);
	return (File) {.contents = buffer, .isError = false};
}

char* fromLastInstance(const char* haystack, const char* needle) {
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

char* inputString(FILE* fp, size_t size) {
	char* str;
	int ch;
	size_t len = 0;
	str = realloc(NULL, sizeof(*str) * size);//size is start size
	if (!str)return str;
	while (EOF != (ch = fgetc(fp)) && ch != '\n') {
		str[len++] = ch;
		if (len == size) {
			str = realloc(str, sizeof(*str) * (size += 16));
			if (!str)return str;
		}
	}
	str[len++] = '\0';

	return realloc(str, sizeof(*str) * len);
}