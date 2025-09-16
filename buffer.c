#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dat.h"

Buffer *newBuffer(void) {
	Buffer *b = malloc(sizeof(Buffer));
	b->_bytes = 0;
	b->_len = 0;
	b->_curptr = b->_bytes;
	return b;
}

Buffer *bufferReserve(Buffer *b, int len) {
	if (b->_bytes) free(b->_bytes);
	b->_bytes = malloc(len);
	b->_len = len;
	b->_curptr = b->_bytes;
	memset(b->_bytes, 0, b->_len);
	return b;
}

int bufferGetCh(Buffer *b) {
	if (b->_curptr + 1 >= b->_bytes + b->_len) {
		return -1;
	}
	return *b->_curptr++;
}

int bufferSeek(Buffer *b, int offset) {
	if (offset < 0) return b->_curptr - b->_bytes;
	if (offset >= b->_len) return -1;
	b->_curptr = b->_bytes + offset;
	return 0;
}

int bufferLen(Buffer *b) {
	return b->_len;
}

int bufferGetAt(Buffer *b, int pos) {
	return b->_bytes[pos];
}

int bufferIsEOF(Buffer *b) {
	return b->_curptr >= b->_bytes+b->_len;
}