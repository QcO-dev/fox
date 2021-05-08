#pragma once
#include <core/common.h>
#include <stdio.h>

typedef struct File {
	char* contents;
	bool isError;
} File;

File readFile(const char* path);

char* fromLastInstance(const char* haystack, const char* needle);

char* inputString(FILE* fp, size_t size);