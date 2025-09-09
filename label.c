#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "dat.h"

int searchLabelsByAddr(Labels *labels, int key)
{
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

// Assumes the capacity is already there
void insertLabel(Labels *ls, int pos, struct Label newvalue)
{
	int i;
	if (ls->len + 1 >= ls->cap) {
		ls->labels = (Label*)realloc(ls->labels, ls->cap * 2);
		ls->cap = ls->cap * 2;
	}
	Label *labels = ls->labels;
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

void addLabel(Labels *ls, char *name, int addr) {
	// sort on address
	Label l = {.name = strdup(name), .addr = addr};
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
		addLabel(labels, name, addr);
         }
}
