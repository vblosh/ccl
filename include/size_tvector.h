#ifndef CCL_SIZE_TVECTOR_H
#define CCL_SIZE_TVECTOR_H

/* Public declaration for the built size_t specialization.  vectorgen.h is a
 * reusable template, so keep the specialization's DATA_TYPE macro private to
 * this include. */
#include "containers.h"
#ifdef DATA_TYPE
#undef DATA_TYPE
#endif
#define DATA_TYPE size_t
#include "vectorgen.h"
#undef DATA_TYPE

#endif /* CCL_SIZE_TVECTOR_H */
