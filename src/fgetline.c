#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include "containers.h"
#include "ccl_internal.h"
/* This code was adapted from the public domain version of fgetline by C.B. Falconer */

static int ValidateGetLineArgs(const void *line_pointer, const void *line_data,
                               const int *n,
                               FILE *stream, ContainerAllocator *mm)
{
	if (stream == NULL || line_pointer == NULL || n == NULL || mm == NULL ||
		*n < 0 ||
		(line_data != NULL && *n == 0)) {
		iError.RaiseError("GetLine",CONTAINER_ERROR_BADARG);
		return CONTAINER_ERROR_BADARG;
	}
	return 0;
}

static int GrowCapacity(int capacity, int *new_capacity)
{
	if (capacity <= 0 || capacity > INT_MAX / 2)
		return 0;
	*new_capacity = capacity * 2;
	return 1;
}

static int WideAllocationSize(int capacity, size_t *bytes)
{
	if (capacity < 0 || (size_t)capacity > SIZE_MAX / sizeof(wchar_t))
		return 0;
	*bytes = (size_t)capacity * sizeof(wchar_t);
	return 1;
}

static int GetDelim(char **LinePointer, int *n, int delimiter, FILE *stream, ContainerAllocator *mm )
{
	char *p,*newp;
	size_t d;
	size_t allocation_size;
	int c;
	int len = 0;
	int new_capacity;

	if (ValidateGetLineArgs(LinePointer, LinePointer ? *LinePointer : NULL,
			n, stream, mm) != 0)
		return CONTAINER_ERROR_BADARG;

	if (!*LinePointer || !*n) {
		p = mm->realloc(*LinePointer, BUFSIZ );
		if (!p) {
			iError.RaiseError("GetLine",CONTAINER_ERROR_NOMEMORY);
			return CONTAINER_ERROR_NOMEMORY;
		}
		*n = BUFSIZ;
		*LinePointer = p;
	}

	else p = *LinePointer;

	/* read until delimiter or EOF */
	while ((c = fgetc( stream )) != EOF) {
		if (len >= *n) {
			d = p - *LinePointer;
			if (!GrowCapacity(*n, &new_capacity))
				goto NoMem;
			newp = mm->realloc(*LinePointer, (size_t)new_capacity );
			if (!newp) 
				goto NoMem;
			p = newp + d;
			*LinePointer = newp;
			*n = new_capacity;
		}
		if (delimiter == c)
			break;
		*p++ = (char) c;
		len++;
		if (len == INT_MAX-1)
			break;
	}

	/* Look for EOF without any bytes read condition */
	if ((c == EOF) && (len == 0))
		return EOF;

	if (len >= *n) {
		d = (size_t)(p - *LinePointer);
		if (*n == INT_MAX)
			goto NoMem;
		allocation_size = (size_t)*n + 1;
		newp = mm->realloc( *LinePointer, allocation_size );
		if (!newp) {
NoMem:
			mm->free(*LinePointer);
			*LinePointer = NULL;
			iError.RaiseError("Getline",CONTAINER_ERROR_NOMEMORY);
			return CONTAINER_ERROR_NOMEMORY;
		}
		p = newp + d;
		*LinePointer = newp;
		*n += 1;
	}
	*p = 0;
	return len;
}

int GetLine(char **LinePointer,int *n, FILE *stream,ContainerAllocator *mm)
{
	return GetDelim(LinePointer,n,'\n',stream,mm);
}

static int WGetDelim(wchar_t **LinePointer, int *n, wint_t delimiter, FILE *stream, ContainerAllocator *mm )
{
	wchar_t *p,*newp;
	size_t d;
	size_t allocation_size;
	wint_t c;
	int len = 0;
	int new_capacity;
	
	if (ValidateGetLineArgs(LinePointer, LinePointer ? *LinePointer : NULL,
			n, stream, mm) != 0)
		return CONTAINER_ERROR_BADARG;
	
	if (!*LinePointer || !*n) {
		if (!WideAllocationSize(BUFSIZ, &allocation_size))
			goto NoMem;
		p = mm->realloc(*LinePointer, allocation_size );
		if (!p) {
			iError.RaiseError("GetLine",CONTAINER_ERROR_NOMEMORY);
			return CONTAINER_ERROR_NOMEMORY;
		}
		*n = BUFSIZ;
		*LinePointer = p;
	}
	
	else p = *LinePointer;
	
	/* read until delimiter or EOF */
	while ((c = getwc( stream )) != WEOF) {
		if (len >= *n) {
			d = p - *LinePointer;
			if (!GrowCapacity(*n, &new_capacity) ||
				!WideAllocationSize(new_capacity, &allocation_size))
				goto NoMem;
			newp = mm->realloc(*LinePointer, allocation_size );
			if (!newp) 
				goto NoMem;
			p = newp + d;
			*LinePointer = newp;
			*n = new_capacity;
		}
		if (delimiter == c)
			break;
		*p++ = (wchar_t) c;
		len++;
		if (len == INT_MAX-1)
			break;
	}
	
	/* Look for EOF without any bytes read condition */
	if ((c == WEOF) && (len == 0))
		return EOF;
	
	if (len >= *n) {
		d = (size_t)(p - *LinePointer);
		if (*n == INT_MAX || !WideAllocationSize(*n + 1, &allocation_size))
			goto NoMem;
		newp = mm->realloc( *LinePointer, allocation_size );
		if (!newp) {
		NoMem:
			mm->free(*LinePointer);
			*LinePointer = NULL;
			iError.RaiseError("Getline",CONTAINER_ERROR_NOMEMORY);
			return CONTAINER_ERROR_NOMEMORY;
		}
		p = newp + d;
		*LinePointer = newp;
		*n += 1;
	}
	*p = 0;
	return len;
}

int WGetLine(wchar_t **LinePointer,int *n, FILE *stream,ContainerAllocator *mm)
{
	return WGetDelim(LinePointer,n,L'\n',stream,mm);
}

#ifdef TEST
int main(void)
{
	char *buf=NULL;
	int n=0;
	int r = GetLine(&buf,&n,stdin);
	printf("'%s'\n",buf);
}
#endif
