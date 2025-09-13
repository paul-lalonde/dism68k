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
	if (pos >= labels->len || labels->labels[pos].addr != key) return -1;
	return pos;
}

void insertLabel(Labels *ls, int pos, struct Label newvalue)
{
	// Renaming the existing label.
	if (pos < ls->len && ls->labels[pos].addr == newvalue.addr) {
		free(ls->labels[pos].name);
		ls->labels[pos] = newvalue;
		return;
	}
	
	// Label ins't a match.  Insert at this position. That means increasing the length by one
	int i;
	if (ls->len == ls->cap) {
		ls->cap = ls->cap * 2;
		Label *nl = (Label*)realloc(ls->labels, sizeof(Label) * ls->cap);
		if (nl == NULL) {
			printf("WTF\n");
		}
		ls->labels = nl;
	}
	for (i = ls->len; i > pos; i--)
		ls->labels[i] = ls->labels[i - 1];
	ls->len++;
	ls->labels[pos] = newvalue;
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
	char *line = 0;
	size_t size = 0;
	while ((nread = getline(&line, &size, fp)) != -1) {
		line[nread-1] = 0; // Ditch the newline
		int addr;
		char name[128];
		sscanf(line, "%x %s", &addr, name);
		addLabel(labels, name, addr, 0);
         }
}

IList *newIList(void) {
	const int cap = 1;
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
	if (l->len+1 >= l->cap) {
		l->cap *= 2;
		l->instrs = (Instruction *)realloc(l->instrs, sizeof(Instruction)*l->cap);
	}
	l->instrs[l->len++] = instr;
}

