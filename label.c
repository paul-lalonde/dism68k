#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "dat.h"


int searchLabelsByAddr(Labels *labels, int key) {
	int l, r, m;
	
	l = 0;
	r = labels->len;
	
	while (l < r) {
		m = l + (r - l) / 2;
		if (labels->labels[m].addr < key)
			l = m + 1;
		else
			r = m;
	}
	return l;
}

int findLabelByAddr(Labels *labels, int key) {
	if (labels->len == 0) return -1;
	int pos = searchLabelsByAddr(labels, key);
	if (labels->labels[pos].addr != key) return -1;
	return pos;
}

// Assumes the capacity is already there.  Overwrites a label if it's already there.
void insertLabel(Labels *ls, int pos, struct Label newvalue)
{
	Label *labels = ls->labels;
	if (labels[pos].addr == newvalue.addr) {
		free(labels[pos].name);
		labels[pos] = newvalue;
		return;
	}
		
	int i;
	if (ls->len + 1 >= ls->cap) {
		ls->labels = (Label*)realloc(ls->labels, ls->cap * 2);
		ls->cap = ls->cap * 2;
	}
	for (i = ls->len; i > pos; i--)
		labels[i] = labels[i - 1];
	ls->len++;
	labels[pos] = newvalue;
}

Labels *newLabels(int cap) {
	Labels *l = (Labels *)malloc(sizeof(Labels));
	l->labels = (Label *)malloc(sizeof( Label ) * cap );
	l->cap = cap;
	l->len = 0;
	return l;
}

void addLabel(Labels *ls, char *name, int addr, int generated) {
	// sort on address
	Label l = {.name = strdup(name), .addr = addr, .generated = generated};
	int ipos = searchLabelsByAddr(ls, addr);
	insertLabel(ls, ipos, l);
}

void freadLabels(FILE *fp, Labels *labels) {
	size_t nread;
	char *line;
	size_t size;
	while ((nread = getline(&line, &size, fp)) != -1) {
		line[nread-1] = 0; // Ditch the newline
		int addr;
		char name[128];
		sscanf(line, "%x %s", &addr, name);
		addLabel(labels, name, addr, 0);
         }
}

IList *newIList(int cap) {
	IList *l = (IList *)malloc(sizeof(IList));
	l->len = 0;
	l->cap = cap;
	l->instrs = (Instruction *)malloc(sizeof(Instruction) * l->cap);
	return l;
}

void clearIList(IList *l) {
	for(int i=0; i < l->len; i++) {
		free(l->instrs[i].asm);
		free(l->instrs[i].instr);
	}
	l->len = 0;
}

void freeIList(IList *l) {
	clearIList(l);
	free(l);
}
	
void appendInstruction(IList *l, Instruction instr) {
	l->instrs[l->len++] = instr;
	if (l->len >= l->cap) {
		l->cap *= 2;
		l->instrs = (Instruction *)realloc(l->instrs, sizeof(Instruction)*l->cap);
	}
}

