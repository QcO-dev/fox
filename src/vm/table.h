#pragma once
#include <core/common.h>
#include <vm/value.h>

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	int count;
	int capacity;
	Entry* entries;
} Table;

typedef struct VM VM;

void initTable(Table* table);

void freeTable(VM* vm, Table* table);

bool tableSet(VM* vm, Table* table, ObjString* key, Value value);

void tableAddAll(VM* vm, Table* from, Table* to);

bool tableGet(Table* table, ObjString* key, Value* value);

bool tableDelete(Table* table, ObjString* key);

ObjString* tableFindString(Table* table, const char* chars, size_t length, uint32_t hash);

void tableRemoveWhite(Table* table);