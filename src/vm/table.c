#include "table.h"

#include <core/memory.h>
#include <vm/object.h>
#include <vm/vm.h>
#include <string.h>
#include <stdio.h>

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
	table->count = 0;
	table->capacity = -1;
	table->entries = NULL;
}

void freeTable(VM* vm, Table* table) {
	FREE_ARRAY(vm, Entry, table->entries, table->capacity + 1);
	initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
	uint32_t index = key->hash & capacity;
	Entry* tombstone = NULL;

	for (;;) {
		Entry* entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NULL(entry->value)) {
				// Empty entry.
				return tombstone != NULL ? tombstone : entry;
			}
			else {
				// We found a tombstone.
				if (tombstone == NULL) tombstone = entry;
			}
		}
		else if (entry->key == key) {
			// We found the key.
			return entry;
		}

		index = (index + 1) & capacity;
	}
}

static void adjustCapacity(VM* vm, Table* table, int capacity) {
	Entry* entries = ALLOCATE(vm, Entry, capacity + 1);
	for (int i = 0; i <= capacity; i++) {
		entries[i].key = NULL;
		entries[i].value = NULL_VAL;
	}

	table->count = 0;
	for (int i = 0; i <= table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key == NULL) continue;

		Entry* dest = findEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(vm, Entry, table->entries, table->capacity + 1);

	table->entries = entries;
	table->capacity = capacity;
}

bool tableSet(VM* vm, Table* table, ObjString* key, Value value) {
	if (table->count + 1 > (table->capacity + 1) * TABLE_MAX_LOAD) {
		int capacity = (table->capacity + 1 < 8 ? 8 : (table->capacity + 1) * 2) - 1;
		adjustCapacity(vm, table, capacity);
	}

	Entry* entry = findEntry(table->entries, table->capacity, key);

	bool isNewKey = entry->key == NULL;
	if (isNewKey && IS_NULL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

bool tableDelete(Table* table, ObjString* key) {
	if (table->count == 0) return false;

	// Find the entry.
	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	// Place a tombstone in the entry.
	entry->key = NULL;
	entry->value = BOOL_VAL(true);

	return true;
}

ObjString* tableFindString(Table* table, const char* chars, size_t length, uint32_t hash) {
	if (table->count == 0) return NULL;

	uint32_t index = hash & table->capacity;

	for (;;) {
		//TODO what

		Entry* entry = &table->entries[index];

		if (entry->key == NULL) {
			// Stop if we find an empty non-tombstone entry.
			if (IS_NULL(entry->value)) return NULL;
		}
		else if (entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0) {
			// We found it.
			return entry->key;
		}

		index = (index + 1) & table->capacity;
	}
	return NULL;
}

void tableRemoveWhite(Table* table) {

	for (int i = 0; i <= table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->obj.isMarked) {
			tableDelete(table, entry->key);
		}
	}

}

void tableAddAll(VM* vm, Table* from, Table* to) {
	for (int i = 0; i <= from->capacity; i++) {
		Entry* entry = &from->entries[i];
		if (entry->key != NULL) {
			tableSet(vm, to, entry->key, entry->value);
		}
	}
}