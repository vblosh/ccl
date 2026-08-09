#ifndef CCL_WSTRINGLIST_H
#define CCL_WSTRINGLIST_H

#include "containers.h"
#include <wchar.h>
#undef CHARTYPE
#undef DATA_TYPE

#define DATA_TYPE wString
#define CHARTYPE wchar_t
#include "stringlistgen.h"

#endif /* CCL_WSTRINGLIST_H */
