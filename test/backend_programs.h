#ifndef CHIBI_BACKEND_PROGRAMS_H
#define CHIBI_BACKEND_PROGRAMS_H

#include "../src/common.h"

extern const char ARITHMETIC[];
extern const char CONTROL[];
extern const char POINTERS[];
extern const char ARRAYS[];
extern const char STRUCTS[];
extern const char SLICES[];
extern const char STORED_SLICES[];
extern const char PASSED_STRUCTS[];
extern const char LEFT_TO_RIGHT[];
extern const char LARGE_STRUCTS[];
extern const char CONSTANT_CONDITIONS[];
extern const char CONSTANT_TABLES[];
extern const char SWITCHES[];
extern const char GLOBALS[];
extern const char EMPTY_GLOBALS[];
extern const char HIDDEN_CONTEXT[];
extern const char MANY_GRID[];
extern const char BIT_COUNTS[];
extern const char BIT_GRID[];
extern const char OVERFLOW_TESTS[];
extern const char OVERFLOW_GRID[];
extern const char MUL_HIGH[];
extern const char BITCASTS[];
extern const char OPTIONALS[];
extern const char STRINGS[];
extern const char VALUE_IFS[];
extern const char STRING_TABLES[];
extern const char STRUCT_IFS[];
extern const char OPTIONAL_FNS[];

extern const char *const BACKEND_PROGRAMS[];
extern const u32 NBACKEND_PROGRAMS;

#endif
