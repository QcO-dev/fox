#pragma once
#include <stdio.h>
char* readFile(const char* path);

char* fromLastInstance(const char* haystack, const char* needle);

char* inputString(FILE* fp, size_t size);