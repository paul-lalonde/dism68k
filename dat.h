typedef struct DisLine DisLine;
typedef struct Listing Listing;
typedef struct Buffer Buffer;

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

extern Listing listing;

extern void loadmap(char *name);
