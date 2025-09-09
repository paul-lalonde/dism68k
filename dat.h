typedef struct DisLine DisLine;
typedef struct Listing Listing;
typedef struct Buffer Buffer;
typedef struct Label Label;
typedef struct Labels Labels;

struct Buffer {
	unsigned char *bytes;
	size_t len;
	unsigned char *curptr;
};

struct DisLine {
	char *asm;
	int addr;
};

struct Listing {
	DisLine **lines;
	int nlines;
	int usedlines;
};

struct Label {
	char *name;
	int addr;
};

struct Labels {
	Label *labels;
	int len;
	int cap;
};

Labels *newLabels(int cap);
void addLabel(Labels *ls, char *name, int addr);
void freadLabels(FILE *fp, Labels *);
int searchLabelsByAddr(Labels *labels, int key);

extern Listing listing;

extern void loadmap(char *name);
extern void loadanddis(Buffer *, Labels *);
