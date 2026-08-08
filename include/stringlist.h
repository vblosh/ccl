#ifndef CCL_STRINGLIST_H
#define CCL_STRINGLIST_H

#include "containers.h"

#undef CHARTYPE
#undef DATA_TYPE
#define CHARTYPE char
#define DATA_TYPE String
#include "stringlistgen.h"

#endif /* CCL_STRINGLIST_H */
