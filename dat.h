typedef struct BasicBlock BasicBlock;
typedef struct Buffer Buffer;
typedef struct IList IList;
typedef struct Instruction Instruction;
typedef struct Label Label;
typedef struct Labels Labels;

struct Buffer {
	unsigned char *bytes;
	size_t len;
	unsigned char *curptr;
};

#define MAXLABELLEN 64
struct Label {
	char *name;
	int addr;
	int generated;
};

struct Labels {
	Label *labels;
	int len;
	int cap;
};

struct BasicBlock {
	int begin, end;
	int ninstr;
	int lineno, nlines;
	int isdata;
	int nbytes;
};

Labels *newLabels(int cap);
void addLabel(Labels *ls, char *name, int addr, int generated); // Strdups the name
void freadLabels(FILE *fp, Labels *);
int searchLabelsByAddr(Labels *labels, int key); // Return insertion point
int findLabelByAddr(Labels *labels, int key); // Return -1 if not found

extern void loadmap(char *name);


enum IncrType {
	PREDECR = 1,
	POSTINCR = 2,
	NOINCR = 0
};

enum OperandType {
	DATA_REG = 1,
	ADDR_REG = 2,
	MEMORY = 3,
};

// Representation of a m68k instruction
struct Instruction {
	char *asm;
	char *instr;
	int address;
	int opnum;
	int nbytes; // encoding size
	int isBranch;
	int isJump;
	int isRet;
	int targetAddress;

	enum OperandType src, dst;
	enum IncrType prepost; 
	int soperand, doperand;	// operands - Data register 
};

struct IList {
	Instruction *instrs; // Array of Instruction
	int len;
	int cap;
};

IList *newIList(void);
void freeIList(IList *);
void clearIList(IList *);
void appendInstruction(IList *, int addr, Instruction);

int rundis(Buffer *bin, BasicBlock *blocks, int nblocks, Labels *labels, IList *instrs);
extern int disasm(Buffer *bin, unsigned long int start, unsigned long int end, Labels *labels, IList *, int justOne);
extern int disasmone(Buffer *bin, int start, Instruction *retval, Labels *labels);
int datadump(Buffer *gBuf, uint32_t start, uint32_t end, void (*write)(char *, int addr, void *), void *d, int restrictline);

void findBasicBlocks(Buffer *bin, BasicBlock **out, int *outlen, int **invalid, int *ninvalid);
int findAddr(int addr, BasicBlock *blocks, int nblocks);
int findBBbyline(BasicBlock *blocks, int nblocks, int line);
int linetoaddr(Buffer *bin, BasicBlock *blocks, int nblocks, int line);
