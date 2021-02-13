#pragma once
#include <core/common.h>
typedef struct {
	size_t capacity;
	size_t count;
	size_t* lines;
} LineNumberTable;

typedef struct VM VM;

void initLineNumberTable(LineNumberTable* table);

void writeLineNumberTable(VM* vm, LineNumberTable* table, size_t index, size_t line);

void freeLineNumberTable(VM* vm, LineNumberTable* table);