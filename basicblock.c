/* 
Hi Claude. Write me a C function that given a motorola 68000 binary program will build a list of basic blocks of that program.
The signature of the function should be 'void findBasicBlocks(unsigned char *bin, int binlen, int outarray, int *outarraylen, int invalid, int *ninvalid)'
The binary to disassemble is passed in as bin and is binlen bytes long.
The function returns an array of integer pairs stored sequentially indicating the start and end addresses of every basic block. The array should be resized automatically as it grows.  
If the walk to find basic blocks meets an invalid instruction, flag it by adding it to the invalid array.
To disassemble the binary, call the function "extern bool disasmoneone(char *bin, int binlen, int start, Instruction *retval);
"; The instruction type is 'struct Instruction { int address; int nbytes; bool isBranch; bool isRet; int targetAddress; }' The field nbytes gives you the instruction boundaries. If the instruction is a branch, isBranch will be set. If the instruction is a return from subroute, isRet will be set. If the instruction is a branch, targetAddress will be set to the target address.  To use disasmone pass the whole binary in, and pass the offset within that binary at which to disassemble in 'start'.  disasmone returns true if the instruction was valid, false if it could not be decoded.
The basic blocks should be returned in sorted order.  If you must sort, remember that basic blocks are already in almost sorted order.
Also write another function that given the basic blocks list will return a list of begin,end pairs of integers corresponding to regions of the binary that are not included in the basic blocks.  This list should be in ordered from lowest begining address to highest.
The function signature should be 'findDataRegions(int *basicblocks,  int **retpairs, int *retcount)'. don't worry about data found after the last basic block.
Avoid using any external libraries.  If you need help with instruction decoding, ask for help. Do not explain your work. If you find the task too difficult, pause and ask me for help. If you need clarification, pause and ask for help.
*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "dat.h"

// returns offset of basic block.
int findAddr(int addr, BasicBlock *blocks, int nblocks) {
	int l, r, m;
	
	l = 0;
	r = nblocks;
	
	while (l < r) {
		m = l + (r - l) / 2;
		if (blocks[m].end <= addr) // We look at the *end* of the blocks.
			l = m + 1;
		else
			r = m;
	}
	return l;
}

// Counts lines and fixes up nlines.
int countlines(BasicBlock *blocks, int nblocks) {
	int lineno = 0;
	for(int i=0; i < nblocks; i++) {
		blocks[i].lineno = lineno;
		if (blocks[i].isdata) lineno += 1; 
		else lineno += blocks[i].ninstr;
	}
}

void findBasicBlocks(Buffer *bin, BasicBlock **outblocks, int *nblocks, int **invalid, int *ninvalid) {
	BasicBlock *blocks = NULL;
	int blockCount = 0;
	int blockCapacity = 0;
	
	int *invalidAddrs = NULL;
	int invalidCount = 0;
	int invalidCapacity = 0;
	
	// Track which addresses are block leaders
	bool *isLeader = (bool*)calloc(bin->len, sizeof(bool));
	if (!isLeader) return;
	
	// Track visited addresses to avoid infinite loops
	bool *visited = (bool*)calloc(bin->len, sizeof(bool));
	if (!visited) {
		free(isLeader);
		return;
	}
	
	// Stack for addresses to process
	int *stack = (int*)malloc(bin->len * sizeof(int));
	if (!stack) {
		free(isLeader);
		free(visited);
		return;
	}
	int stackTop = 0;
	
	// Start at address 0
	isLeader[0] = true;
	stack[stackTop++] = 0;
	
	// First pass: find all reachable code and mark leaders
	while (stackTop > 0) {
		int addr = stack[--stackTop];
		
		if (addr >= bin->len || visited[addr]) continue;
		Labels labels = {.len = 0};
		struct Instruction inst;
		if (!disasmone(bin, addr, &inst, &labels)) {
			// Invalid instruction
			if (invalidCount >= invalidCapacity) {
				invalidCapacity = invalidCapacity ? invalidCapacity * 2 : 16;
				invalidAddrs = realloc(invalidAddrs, sizeof(int) * invalidCapacity);
				if (!invalidAddrs) break;
			}
			invalidAddrs[invalidCount++] = addr;
			visited[addr] = true;
			continue;
		}
		
		visited[addr] = true;
		int nextAddr = addr + inst.nbytes;
		
		if (inst.isBranch || inst.isJump) {
			// Target of branch is a leader
//printf("bin->len = %ld\n", bin->len);
//printf("addr = 0x%0x\n", addr);
//printf("inst.targetAddress = %x\n", inst.targetAddress);
			if (inst.targetAddress < bin->len) {
				isLeader[inst.targetAddress] = true;
				if (!visited[inst.targetAddress]) {
					stack[stackTop++] = inst.targetAddress;
				}
			}
			
			// Instruction after branch is a leader (for conditional branches)
			if (inst.isBranch && (nextAddr < bin->len)) {
				isLeader[nextAddr] = true;
				if (!visited[nextAddr]) {
					stack[stackTop++] = nextAddr;
				}
			}
		} else if (inst.isRet) {
			// Don't follow after return
		} else {
			// Continue to next instruction
			if (nextAddr < bin->len) {
				if (!visited[nextAddr]) {
					stack[stackTop++] = nextAddr;
				}
			}
		}
	}
	
	// Second pass: build basic blocks
	for (int addr = 0; addr < bin->len; addr++) {
		if (!isLeader[addr] || !visited[addr]) continue;
		
		int blockStart = addr;
		int currentAddr = addr;
		int ninstr = 0;

		// Find end of basic block
		while (currentAddr < bin->len && visited[currentAddr]) {
			struct Instruction inst;
			Labels labels = {.len = 0};
			if (!disasmone(bin, currentAddr, &inst, &labels)) {
				break;
			}
			
			ninstr++;
			int nextAddr = currentAddr + inst.nbytes;
			
			// Block ends if:
			// 1. Next instruction is a leader
			// 2. This is a branch or return
			// 3. We reach end of binary
			if (nextAddr >= bin->len || 
				(nextAddr < bin->len && isLeader[nextAddr]) ||
				inst.isBranch || inst.isJump || inst.isRet) {
				
				// Add block
				if (blockCount >= blockCapacity) {
					blockCapacity = blockCapacity ? blockCapacity * 2 : 32;
					blocks = realloc(blocks, sizeof(BasicBlock) * blockCapacity);
					if (!blocks) break;
				}
				
				blocks[blockCount].begin = blockStart;
				blocks[blockCount].end = currentAddr + inst.nbytes;
				blocks[blockCount].ninstr = ninstr;
				blocks[blockCount].lineno = -1;
				blocks[blockCount].isdata = 0;
				blockCount++;
				break;
			}
			
			currentAddr = nextAddr;
		}
	}
	
	// Sort blocks by start address

	int len = blockCount;
	for (int i = 1; i < len; i ++) {
		int startAddr = blocks[i].begin;
		int endAddr = blocks[i].end;
		int ninstr = blocks[i].ninstr;
		int j = i - 1;
		
		while (j >= 0 && blocks[j].begin > startAddr) {
			blocks[j + 1].begin = blocks[j].begin;
			blocks[j + 1].end = blocks[j].end;
			blocks[j + 1].ninstr = blocks[j].ninstr;
			j -= 1;
		}
		
		blocks[j + 1].begin = startAddr;
		blocks[j + 1].end = endAddr;
		blocks[j + 1].ninstr = ninstr;
		blocks[j + 1].isdata = 0;
		blocks[j + 1].nlines = ninstr;
	}

	// Find data sections - they lie between basic blocks; we insert them.
	for(int i = 1; i < blockCount; i++) {
		if (blocks[i-1].end != blocks[i].begin) {
			// Add block
			blockCount++;
			if (blockCount >= blockCapacity) {
				blockCapacity = blockCapacity ? blockCapacity * 2 : 32;
				blocks = realloc(blocks, sizeof(BasicBlock) * blockCapacity);
				assert(blocks);
			}
			// Shift blocks up
			for(int j = blockCount - 1; j >= i; j--) {
				blocks[j ].begin = blocks[j-1].begin;
				blocks[j ].end = blocks[j-1].end;
				blocks[j ].ninstr = blocks[j-1].ninstr;
				blocks[j ].isdata = blocks[j-1].isdata;
			}
			blocks[i].begin = blocks[i].end;
			blocks[i].end = blocks[i+1].begin;
			blocks[i].ninstr = 0; //((blocks[i].end - blocks[i].begin)+7) % 8; // we will present at most 16 bytes per line; instrs count in pairs
			blocks[i].nlines = 1;
			blocks[i].isdata = true;			
		}
	}
			

	// Count lines
	// TODO(PAL): This will break when we add data blocks.
	countlines(blocks, blockCount);
	// Clean up
	free(isLeader);
	free(visited);
	free(stack);
	
	// Return results
	*outblocks = blocks;
	*nblocks = blockCount;
	*invalid = invalidAddrs;
	*ninvalid = invalidCount;
}


// return BB index containing line
int findBBbyline(BasicBlock *blocks, int nblocks, int line) {
	int l, r, m;
	
	l = 0;
	r = nblocks;
	
	while (l <= r) {
		m = l + (r - l) / 2;

		if (line >= blocks[m].lineno  && line < blocks[m+1].lineno)
			return m;
 
		if (blocks[m].lineno < line )
			l = m + 1;
		else
			r = m;
	}
	return l;
}

int linetoaddr(Buffer *bin, BasicBlock *blocks, int nblocks, int line) {
	if (line == 95) {
		printf("");
	}
	int bb = findBBbyline(blocks, nblocks, line);
	if (blocks[bb].isdata) {
		return blocks[bb].begin; // blocks[bb].end - blocks[bb].begin;
	}
	Instruction instr;
	int lineno = blocks[bb].lineno; 
	int addr = blocks[bb].begin;
	while (lineno != line && addr < blocks[bb].end) {
		Labels labels = {.len = 0};
		disasmone(bin, addr, &instr, &labels);
		addr += instr.nbytes;
		lineno++;
	}
	return addr;
}
