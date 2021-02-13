#include "lineNumber.h"
#include <core/memory.h>
#include <stdio.h>

void initLineNumberTable(LineNumberTable* table) {
	table->lines = NULL;
	table->capacity = 0;
	table->count = 0;
}

void writeLineNumberTable(LineNumberTable* table, size_t index, size_t line) {

	if (table->count == 0) {
		table->capacity = 8;
		table->lines = GROW_ARRAY(size_t, table->lines, 0, table->capacity);

		table->lines[table->count] = index;
		table->lines[table->count + 1] = line;
		table->count += 2;
	}
	else {
		if (table->lines[table->count - 1] != line) {

			if (table->capacity < table->count + 2) {
				size_t oldCap = table->capacity;
				table->capacity = table->capacity < 8 ? 8 : table->capacity * 2;
				table->lines = GROW_ARRAY(size_t, table->lines, oldCap, table->capacity);
			}

			table->lines[table->count] = index;
			table->lines[table->count + 1] = line;
			table->count += 2;
		}

	}

}

void freeLineNumberTable(LineNumberTable* table) {
	FREE_ARRAY(size_t, table->lines, table->capacity);
	initLineNumberTable(table);
}