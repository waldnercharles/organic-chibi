#ifndef CHIBI_C_SOURCE_H
#define CHIBI_C_SOURCE_H

#include "check.h"

void c_source_prelude(FILE *out);
const char *c_source_unsupported(const Checker *c);
const char *c_source_package(FILE *out, const Checker *c, const char *prefix);
void c_source_runtime(FILE *out);

#endif
