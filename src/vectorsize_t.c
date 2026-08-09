#include "containers.h"
#undef DATA_TYPE
#define DATA_TYPE size_t
#define VECTORGEN_IMPLEMENTATION
#include "vectorgen.c"
#undef VECTORGEN_IMPLEMENTATION
