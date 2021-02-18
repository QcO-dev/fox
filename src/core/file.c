#include <core/common.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".", path);
		return NULL;
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".", path);
		return NULL;
	}


	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytesRead] = '\0';

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".", path);
		return NULL;
	}

	fclose(file);
	return buffer;
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