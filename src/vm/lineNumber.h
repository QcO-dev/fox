#pragma once
#include <core/common.h>
typedef struct {
	size_t capacity;
	size_t count;
	size_t* lines;
} LineNumberTable;

void initLineNumberTable(LineNumberTable* table);

void writeLineNumberTable(LineNumberTable* table, size_t index, size_t line);

void freeLineNumberTable(LineNumberTable* table);