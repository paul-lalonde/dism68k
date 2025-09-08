typedef struct DisLine DisLine;
typedef struct Listing Listing;

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
