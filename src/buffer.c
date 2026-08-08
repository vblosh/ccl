#include "containers.h"

struct _StreamBuffer {
	size_t Size;
	size_t Cursor;
	unsigned Flags;
	ContainerAllocator *Allocator;
	char *Data;
} ;

#define SB_READ_FLAG   1
#define SB_WRITE_FLAG  2
#define SB_APPEND_FLAG 4
#define SB_FIXED       8

#define FCLOSE_DESTROYS 8

static int Finalize(StreamBuffer *b);
static StreamBuffer *CreateWithAllocator(size_t size,const ContainerAllocator *Allocator)
{
	StreamBuffer *result;
	if (Allocator == NULL || Allocator->malloc == NULL ||
		Allocator->free == NULL || Allocator->realloc == NULL) {
		iError.RaiseError("iStreamBuffer.CreateWithAllocator",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	if (size == 0) 
		size = 1024;
	result = Allocator->malloc(sizeof(StreamBuffer));
	if (result == NULL) {
		iError.RaiseError("iStreamBuffer.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	memset(result,0,sizeof(StreamBuffer));
	result->Allocator = (ContainerAllocator *)Allocator;
	result->Data = Allocator->malloc(size);
	if (result->Data == NULL) {
		Allocator->free(result);
		iError.RaiseError("iStreamBuffer.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	result->Size = size;
	memset(result->Data,0,size);
	return result;
}

static StreamBuffer *Create(size_t size)
{
	return CreateWithAllocator(size,CurrentAllocator);
}

static StreamBuffer *CreateFromFile(const char *FileName)
{
	FILE *f;
	StreamBuffer *result = NULL;
	size_t siz;
	long offset;

	if (FileName == NULL) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	f = fopen(FileName,"rb");
	if (f == NULL) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_NOENT,FileName);
		return NULL;
	}
	else if (fseek(f, 0, SEEK_END)) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_FILE_READ);
		goto err;
	}
	offset = ftell(f);
	if (offset < 0 || (uintmax_t)offset > (uintmax_t)(SIZE_MAX - 1)) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_FILE_READ);
		goto err;
	}
	if (fseek(f,0,SEEK_SET)) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_FILE_READ);
		goto err;
	}
	siz = (size_t)offset;
	result = Create(siz+1);
	if (result == NULL) goto err;
	if (siz != fread(result->Data,1,siz,f)) {
		iError.RaiseError("iBuffer.CreateFromFile",CONTAINER_ERROR_FILE_READ);
		Finalize(result);
		result = NULL;
	}
err:
	fclose(f);
	return result;
}

static int enlargeBuffer(StreamBuffer *b,size_t chunkSize)
{
	char *p;
	size_t newSiz;
	size_t growth;

	/* chunkSize is the required capacity, not an additive increment. */
	if (chunkSize <= b->Size)
		return 1;
	growth = b->Size / 2;
	if (growth == 0)
		growth = 1;
	if (b->Size > SIZE_MAX - growth)
		newSiz = SIZE_MAX;
	else
		newSiz = b->Size + growth;
	if (newSiz < chunkSize)
		newSiz = chunkSize;
	p = b->Allocator->realloc(b->Data,newSiz);
	if (p == NULL)
		return CONTAINER_ERROR_NOMEMORY;
	b->Data = p;
	b->Size = newSiz;
	return 1;
}

static size_t Write(StreamBuffer *b,void *data, size_t siz)
{
	size_t required;
	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.Write",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (siz != 0 && data == NULL) {
		iError.RaiseError("iStreamBuffer.Write",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (siz > SIZE_MAX - b->Cursor) {
		iError.RaiseError("iStreamBuffer.Write",CONTAINER_ERROR_BUFFEROVERFLOW);
		return 0;
	}
	required = b->Cursor + siz;
	if (required > b->Size) {
		int r = enlargeBuffer(b,required);
		if (r < 0)
			iError.RaiseError("iStreamBuffer.Write",r);
		if (r < 0)
			return 0;
	}
	if (siz != 0)
		memcpy(b->Data+b->Cursor,data,siz);
	b->Cursor = required;
	return siz;
}

static size_t Read(StreamBuffer *b, void *data, size_t siz)
{
	size_t len;
	if (b == NULL || data == NULL || siz == 0) {
		iError.RaiseError("iStreamBuffer.Read",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (b->Cursor >= b->Size)
		return 0;
	len = siz;
	if (b->Size - b->Cursor < len)
		len = b->Size - b->Cursor;
	memcpy(data,b->Data+b->Cursor,len);
	b->Cursor += len;
	return len;
}

static int SetPosition(StreamBuffer *b,size_t pos)
{
	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.SetPosition",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	if (pos > b->Size)
		pos = b->Size;
	b->Cursor = pos;
	return 1;
}

static size_t GetPosition(const StreamBuffer *b)
{
	if (b == NULL)
		return 0;
	return b->Cursor;
}

static size_t StreamBufferSize(const StreamBuffer *b)
{
	if (b == NULL)
		return sizeof(StreamBuffer);
	return b->Size;
}

static int Clear(StreamBuffer *b)
{
	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.Clear",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	if (b->Data != NULL && b->Size != 0)
		memset(b->Data,0,b->Size);
	b->Cursor = 0;
	return 1;
}

static int Finalize(StreamBuffer *b)
{
	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.Finalize",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	b->Allocator->free(b->Data);
	b->Allocator->free(b);
	return 1;
}

static char *GetData(const StreamBuffer *b)
{
	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.GetData",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	return b->Data;
}

static int Resize(StreamBuffer *b,size_t newSize)
{
	char *tmp;

	if (b == NULL) {
		iError.RaiseError("iStreamBuffer.Resize",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	if (newSize == b->Size)
		return 0;
	if (newSize == 0) {
		b->Allocator->free(b->Data);
		b->Data = NULL;
		b->Size = 0;
		b->Cursor = 0;
		return 1;
	}
	tmp = b->Allocator->realloc(b->Data,newSize);
	if (tmp == NULL) {
		iError.RaiseError("iStreamBuffer.Resize",CONTAINER_ERROR_NOMEMORY);
		return CONTAINER_ERROR_NOMEMORY;
	}
	b->Data = tmp;
	b->Size = newSize;
	if (b->Cursor > newSize)
		b->Cursor = newSize;
	return 1;
}

static int ReadFromFile(StreamBuffer *b,FILE *infile)
{
	if (b == NULL || infile == NULL) {
		iError.RaiseError("iStreamBuffer.ReadFromFile",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	b->Cursor = 0;
	if (b->Size == 0)
		return 0;
	return fread(b->Data,1,b->Size,infile);
}

static int WriteToFile(StreamBuffer *b,FILE *outfile)
{
	if (b == NULL || outfile == NULL) {
		iError.RaiseError("iStreamBuffer.WriteToFile",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	b->Cursor = 0;
	if (b->Size == 0)
		return 0;
	return fwrite(b->Data,1,b->Size,outfile);
}

StreamBufferInterface iStreamBuffer = {
Create,
CreateWithAllocator,
CreateFromFile,
Read,
Write,
SetPosition,
GetPosition,
GetData,
StreamBufferSize,
Clear,
Finalize,
Resize,
ReadFromFile,
WriteToFile,
};
/* --------------------------------------------------------------------------
                               Circular buffers
   ------------------------------------------------------------------------- */
struct _CircularBuffer {
	size_t maxCount;
	size_t ElementSize;
	size_t head;
	size_t tail;
	ContainerAllocator *Allocator;
	DestructorFunction DestructorFn;
	unsigned char *data;
};


static DestructorFunction SetDestructor(CircularBuffer *cb,DestructorFunction fn)
{
	DestructorFunction oldfn;
	if (cb == NULL)
		return NULL;
	oldfn = cb->DestructorFn;
	cb->DestructorFn = fn;
	return oldfn;
}
	
static size_t Size(const CircularBuffer *cb)
{
	if (cb == NULL)
		return sizeof(CircularBuffer);
	return cb->head - cb->tail;
}

static int Add( CircularBuffer * b,const void *data_element)
{
	unsigned char *ring_data;
	int result = 1;
	size_t count;

	if (b == NULL || data_element == NULL) {
        iError.RaiseError("iCircularBuffer.Add",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
	count = b->head - b->tail;
	if (count == b->maxCount) {
		ring_data = b->data + ((b->head % b->maxCount) * b->ElementSize);
		if (b->DestructorFn)
			b->DestructorFn(ring_data);
		b->tail++;
		result = 0;
	} else {
		ring_data = b->data + ((b->head % b->maxCount) * b->ElementSize);
	}
	memcpy(ring_data,data_element,b->ElementSize);
	b->head++;
	return result;
}

static int PopFront(CircularBuffer *b,void *result)
{
	void *data;
	
    if (b == NULL) {
        iError.RaiseError("iCircularBuffer.PopFront",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
	if (b->head == b->tail)
		return 0;
    data = &(b->data[(b->tail % b->maxCount) * b->ElementSize]);
	if (result)
		memcpy(result,data,b->ElementSize);
	if (b->DestructorFn)
		b->DestructorFn(data);
	memset(data,0,b->ElementSize);
	b->tail++;
	return 1;
}

static int PeekFront(CircularBuffer *b,void *result)
{
	void *data;
	
    if (b == NULL || result == NULL) {
        iError.RaiseError("iCircularBuffer.PeekFront",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
	if (b->head == b->tail)
		return 0;
    data = &(b->data[(b->tail % b->maxCount) * b->ElementSize]);
	memcpy(result,data,b->ElementSize);
	return 1;
}

static CircularBuffer *CreateCBWithAllocator(size_t sizElement,size_t sizeBuffer,const ContainerAllocator *allocator)
{
	CircularBuffer *result;
	
	if (sizElement == 0 || sizeBuffer == 0 || allocator == NULL) {
		iError.RaiseError("iCircularBuffer.Create",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	if (allocator->malloc == NULL || allocator->free == NULL) {
		iError.RaiseError("iCircularBuffer.Create",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	if (sizElement > SIZE_MAX / sizeBuffer) {
		iError.RaiseError("iCircularBuffer.Create",CONTAINER_ERROR_BUFFEROVERFLOW);
		return NULL;
	}
	result = allocator->malloc(sizeof(CircularBuffer));
	if (result == NULL) {
		iError.RaiseError("iCircularBuffer.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	memset(result,0,sizeof(CircularBuffer));
	result->maxCount = sizeBuffer;
	result->ElementSize = sizElement;
	result->Allocator = (ContainerAllocator *)allocator;
	sizElement = sizElement*sizeBuffer;
	result->data = allocator->malloc(sizElement);
	if (result->data == NULL) {
		allocator->free(result);
		iError.RaiseError("iCircularBuffer.Create",CONTAINER_ERROR_NOMEMORY);
		return NULL;
	}
	memset(result->data,0,sizElement);
	return result;
}

static CircularBuffer *CreateCB(size_t sizElement,size_t sizeBuffer)
{
	return CreateCBWithAllocator(sizElement, sizeBuffer, CurrentAllocator);
}

static int CBClear(CircularBuffer *cb)
{
	unsigned char *data;
	size_t i;
	size_t count;
	if (cb == NULL) {
		iError.RaiseError("iCircularBuffer.Clear",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	count = cb->head - cb->tail;
	if (count == 0)
		return 0;
	if (cb->DestructorFn) {
		for (i=0; i<count; i++) {
			data = cb->data + (((cb->tail + i) % cb->maxCount) *
				cb->ElementSize);
			cb->DestructorFn(data);
		}
	}
	memset(cb->data,0,cb->maxCount * cb->ElementSize);
	cb->head = cb->tail = 0;
	return 1;
}

static int CBFinalize(CircularBuffer *cb)
{
	if (cb == NULL) {
		iError.RaiseError("iCircularBuffer.Finalize",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	CBClear(cb);
	cb->Allocator->free(cb->data);
	cb->Allocator->free(cb);
	return 1;
}

static size_t Sizeof(const CircularBuffer *cb)
{
	size_t storage;
	if (cb == NULL)
		return sizeof(CircularBuffer);
	storage = cb->maxCount * cb->ElementSize;
	if (storage > SIZE_MAX - sizeof(CircularBuffer))
		return SIZE_MAX;
	return sizeof(CircularBuffer) + storage;
}

CircularBufferInterface iCircularBuffer = {
	Size,
	Add,
	PopFront,
	PeekFront,
	CreateCBWithAllocator,
	CreateCB,
	CBClear,
	CBFinalize,
	Sizeof,
	SetDestructor,
};
