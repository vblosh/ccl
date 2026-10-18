#include "containers.h"
#undef DATA_TYPE
#ifndef CCL_LONGLONG_TYPEDEF
#define CCL_LONGLONG_TYPEDEF
typedef long long longlong;
#endif
#define DATA_TYPE longlong
#define COMPARE_EXPRESSION(A, B) (((B) > (A)) ? -1 : ((B) != (A)))
#include "dlistgen.c"
#undef COMPARE_EXPRESSION
#undef DATA_TYPE
