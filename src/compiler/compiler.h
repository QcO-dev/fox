#pragma once
#include <vm/chunk.h>
#include <vm/vm.h>

ObjFunction* compile(VM* vm, const char* source, Chunk* chunk);
void markCompilerRoots(Compiler* compiler);
typedef struct Compiler Compiler;