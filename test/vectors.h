#ifndef CHIBI_VECTORS_H
#define CHIBI_VECTORS_H

#include "../src/common.h"

typedef struct
{
    const char *name;
    int64_t args[3];
    int64_t want;
    int trap;
} VectorRun;

typedef u64 VectorOracle(int64_t a, int64_t b, int64_t op, int64_t k);

typedef struct
{
    const char *name;
    int64_t lanes;
    VectorOracle *oracle;
} GridFunction;

extern const char VECTOR_BODY[];
extern const char VECTOR_RUNS[];
extern const VectorRun VECTOR_BODY_RESULTS[];
extern const size_t NVECTOR_BODY_RESULTS;
extern const VectorRun VECTOR_RESULTS[];
extern const size_t NVECTOR_RESULTS;
extern const char VECTOR_GRID[];
extern const GridFunction VECTOR_GRID_FUNCTIONS[];
extern const size_t NVECTOR_GRID_FUNCTIONS;
extern const char INT128_PROGRAM[];
extern const VectorRun INT128_RUNS[];
extern const size_t NINT128_RUNS;
extern const char INT128_RELEASE_FAST[];
extern const VectorRun INT128_RELEASE_FAST_RUNS[];
extern const size_t NINT128_RELEASE_FAST_RUNS;
extern const char INT128_CHAIN[];
extern const char INT128_CHAIN_SHIFTED[];
extern const VectorRun INT128_CHAIN_RUNS[];
extern const VectorRun INT128_CHAIN_SHIFTED_RUNS[];

#endif
