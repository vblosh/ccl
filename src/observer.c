#include "containers.h"
#include "ccl_internal.h"
typedef struct _tagObserver {
	void *ObservedObject; /* The object being observed */
	ObserverFunction Callback; /* The function to call */
	unsigned Flags; /* The events the observer wishes to be informed about */
} Observer;

static Observer *ObserverVector;
static size_t vsize;
/* The vector has process lifetime and no public reset operation.  Keep the
   allocator that owns it so a later iAllocator.Change cannot make a growth
   operation pass a block to a different allocator. */
static const ContainerAllocator *ObserverAllocator;
#define CHUNK_SIZE	25

static int Subscribe(void *ObservedObject, ObserverFunction callback, unsigned flags)
{
        size_t i;
        Observer *pObs = NULL;
        const ContainerAllocator *allocator;

        if (ObservedObject == NULL || callback == NULL || flags == 0) {
                iError.RaiseError("iObserver.Subscribe",CONTAINER_ERROR_BADARG);
                return CONTAINER_ERROR_BADARG;
        }

        if (ObserverVector == NULL) {
                allocator = CurrentAllocator;
                if (allocator == NULL || allocator->calloc == NULL) {
                        iError.RaiseError("iObserver.Subscribe",CONTAINER_ERROR_NOMEMORY);
                        return CONTAINER_ERROR_NOMEMORY;
                }
                ObserverVector = allocator->calloc(CHUNK_SIZE,
                                                   sizeof(*ObserverVector));
                if (ObserverVector == NULL) {
                        iError.RaiseError("iObserver.Subscribe",CONTAINER_ERROR_NOMEMORY);
                        return CONTAINER_ERROR_NOMEMORY;
                }
                ObserverAllocator = allocator;
                vsize = CHUNK_SIZE;
        }
        for (i = 0; i < vsize; i++) {
                if (ObserverVector[i].ObservedObject == NULL) {
                        pObs = ObserverVector + i;
                        break;
                }
        }
        if (pObs == NULL) {
                size_t old_size = vsize;
                size_t new_size;
                Observer *tmp;

                if (old_size > (size_t)-1 - CHUNK_SIZE)
                        goto no_memory;
                new_size = old_size + CHUNK_SIZE;
                if (new_size > (size_t)-1 / sizeof(*ObserverVector))
                        goto no_memory;
                allocator = ObserverAllocator;
                if (allocator == NULL || allocator->realloc == NULL)
                        goto no_memory;
                tmp = allocator->realloc(ObserverVector,
                                         new_size * sizeof(*ObserverVector));
                if (tmp == NULL)
                        goto no_memory;
                ObserverVector = tmp;
                memset(ObserverVector + old_size, 0,
                       CHUNK_SIZE * sizeof(*ObserverVector));
                pObs = ObserverVector + old_size;
                vsize = new_size;
        }

        /* No operation after this point can fail, so the subject flag is
           changed only after the relationship has been committed. */
        pObs->ObservedObject = ObservedObject;
        pObs->Callback = callback;
        pObs->Flags = flags;
        ((GenericContainer *)ObservedObject)->Flags |= CONTAINER_HAS_OBSERVER;
        return 1;

no_memory:
        iError.RaiseError("iObserver.Subscribe",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
}


static int Notify(const void *ObservedObject,unsigned operation,const void *ExtraInfo1,const void *ExtraInfo2)
{
	int count=0;
	size_t idx;
	size_t scan_limit;
	const void *ExtraInfo[2];

	if (ObservedObject == NULL || operation == 0) {
		iError.RaiseError("iObserver.Notify",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}

	ExtraInfo[0] = ExtraInfo1;
	ExtraInfo[1] = ExtraInfo2;
	/* A callback may subscribe while notification is in progress.  Do not
	   chase a table grown by that callback; the notification covers the
	   table extent that existed when it began. */
	scan_limit = vsize;
	for (idx=0; idx < scan_limit;idx++) {
		if (ObserverVector[idx].ObservedObject == ObservedObject) {
			if (ObserverVector[idx].Flags & operation) {
				ObserverVector[idx].Callback(ObservedObject,operation,ExtraInfo);
				count++;
			}
		}
	}
	return count;
}

static size_t Unsubscribe(void *ObservedObject,ObserverFunction callback)
{
	size_t idx=0,count=0;

	if (ObservedObject == NULL) {
		/* Erase all observers that have the specified function. This means that the
		  object receiving the callback goes out of scope */
		if (callback) /* If both are NULL do nothing */
			for (; idx<vsize;idx++) {
				if (ObserverVector[idx].Callback == callback) {
					memset(ObserverVector+idx,0,sizeof(Observer));
					count++;
				}
		}
	}
	else if (callback == NULL) {
		for (;idx<vsize;idx++) {
			if (ObserverVector[idx].ObservedObject == ObservedObject) {
				memset(ObserverVector+idx,0,sizeof(Observer));
				count++;
			}
		}
	}
	else for (; idx<vsize;idx++) {
		if (ObserverVector[idx].ObservedObject == ObservedObject &&
			ObserverVector[idx].Callback == callback) {
			memset(ObserverVector+idx,0,sizeof(Observer));
			count++;
		}
	}
	return count;
}

ObserverInterface iObserver = {
	Subscribe,
	Notify,
	Unsubscribe
};
