#ifndef CCL_LONGLONGLIST_H
#define CCL_LONGLONGLIST_H
#include "containers.h"
#undef DATA_TYPE
#ifndef CCL_LONGLONG_TYPEDEF
#define CCL_LONGLONG_TYPEDEF
typedef long long longlong;
#endif
#define DATA_TYPE longlong
#include "listgen.h"
#undef DATA_TYPE
#undef LIST_TYPE
#undef LIST_TYPE_
#undef INTERFACE
#undef ITERATOR
#undef ITERFACE_NAME
#undef INTERFACE_NAME
#undef LIST_STRUCT_INTERNAL_NAME
#undef INTERFACE_STRUCT_INTERNAL_NAME
#undef LIST_ELEMENT
#undef LIST_ELEMENT_
#undef CONCAT
#undef CONCAT3_
#undef CONCAT3
#undef EVAL
#undef ERROR_RETURN
#undef DEFAULT_START_SIZE
#endif
