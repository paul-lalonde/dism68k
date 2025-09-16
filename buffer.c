#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dat.h"


	
Section *newSection(void) {
	Section *b = malloc(sizeof(Section));
	b->_bytes = 0;
	b->_len = 0;
	b->_curptr = b->_bytes;
	b->_name = strdup("noname");
	b->_baseaddress = 0;
	return b;
}

Section *sectionReserve(Section *b, int len) {
	if (b->_bytes) free(b->_bytes);
	b->_bytes = malloc(len);
	b->_len = len;
	b->_curptr = b->_bytes;
	memset(b->_bytes, 0, b->_len);
	return b;
}

int sectionGetCh(Section *b) {
	if (b->_curptr + 1 >= b->_bytes + b->_len) {
		return -1;
	}
	return *(b->_curptr++);
}

int sectionSeek(Section *b, int offset) {
	if (offset < 0) return b->_curptr - b->_bytes;
	if (offset >= b->_len) return -1;
	b->_curptr = b->_bytes + offset;
	return 0;
}

int sectionLen(Section *b) {
	return b->_len;
}

// Relative to start of section.
int sectionGetAt(Section *b, int pos) { 
	return b->_bytes[pos];
}

int sectionIsEOF(Section *b) {
	return b->_curptr >= b->_bytes+b->_len;
}

Buffer *newBuffer(void) {
	Buffer *b = malloc(sizeof(Buffer));
	b->cap = 16;
	b->sections = malloc(sizeof(Section) * b->cap);
	b->len = 0;
	return b;
}

int bufferGetCh(Buffer *b) {
	return sectionGetCh(&b->sections[0]);
}

int bufferSeek(Buffer *b, int offset) {
	for(int s = 0; s < b->len; s++) {
		if (b->sections[s]._baseaddress <= offset && offset < b->sections[s]._baseaddress + b->sections[s]._len)
			sectionSeek(&b->sections[s], offset);
	}
	return -1;
}

int bufferLen(Buffer *b /*, int section*/) {
	return b->sections[0]._len;
}
	
int bufferGetAt(Buffer *b, int offset) {
	for(int s = 0; s < b->len; s++) {
		if (b->sections[s]._baseaddress <= offset && offset < b->sections[s]._baseaddress + b->sections[s]._len)
			return sectionGetAt(&b->sections[s], offset-b->sections[s]._baseaddress);
	}
	panic("bufferGetAt offset %d not mapped\n", offset);
	return -1;
}

int bufferIsEOF(Buffer *b) {
	return sectionIsEOF(&b->sections[0]);
}

void bufferAddSection(Buffer *b, int base, int len, char *name) {
	Section *s = newSection();
	s->_name = name;
	s->_len = len;
	s->_baseaddress = base;
	s->_curptr = s->_bytes;
	
	if (b->len + 1 >= b->cap) {
		panic("Increase section array size");
	}
	
	// Insert in sorted order
	int i;
	for(i = 0; i < b->len; i++) {
		if (b->sections[i]._baseaddress < s->_baseaddress && s->_baseaddress < b->sections[i]._baseaddress + len) {						for(int j = b->len; j > i; j++) {
				b->sections[j] = b->sections[j-1];
			}
			break;
		}
	}
	b->sections[i] = *s;
	b->len++;
}

int bufferSectionByName(Buffer *b, char *name) {
	for(int i=0; i < b->len; i++) {
		if (strcmp(name, b->sections[i]._name) == 0) {
			return i;
		}
	}
	return -1;
}

int bufferSectionByAddr(Buffer *b, int addr) {
	for(int s = 0; s < b->len; s++) {
		if (b->sections[s]._baseaddress <= addr && addr < b->sections[s]._baseaddress + b->sections[s]._len)
			return s;
	}
	return -1;
}

int bufferIsMappedAddress(Buffer *b, int addr) {
	for(int s = 0; s < b->len; s++) {
		if (b->sections[s]._baseaddress <= addr && addr < b->sections[s]._baseaddress + b->sections[s]._len)
			return 1;
	}
	return 0;
}

int bufferEndAddress(Buffer *b) {
	return b->sections[b->len-1]._baseaddress + b->sections[b->len-1]._len;
}
