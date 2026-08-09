#ifndef CCL_LONGLONGDLIST_H
#define CCL_LONGLONGDLIST_H
#include "containers.h"
#undef DATA_TYPE
typedef long long longlong;
#define DATA_TYPE longlong
#include "dlistgen.h"
#undef DATA_TYPE
#undef LIST_TYPE
#undef LIST_TYPE_
#undef INTERFACE
#undef ITERATOR
#undef INTERFACE_NAME
#undef ITERFACE_NAME
#undef LIST_ELEMENT
#undef LIST_ELEMENT_
#undef LIST_STRUCT_INTERNAL_NAME
#undef INTERFACE_STRUCT_INTERNAL_NAME
#undef CONCAT
#undef CONCAT3_
#undef CONCAT3
#undef EVAL
#undef ERROR_RETURN
#endif
