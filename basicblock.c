#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "dat.h"

#define MIN(X,Y) ((X)<(Y)?(X):(Y))
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
int countlines(Buffer *bin, BasicBlock *blocks, int nblocks) {
	int lineno = 0;
	for(int i=0; i < nblocks; i++) {
		// Count lines in bb.
		int count = 0;
		for(int j=0; j < blocks[i].instructions->len; j++) {
			if (blocks[i].instructions->instrs[j].isRet) {
				blocks[i].instructions->instrs[j].nlines++;
			}
			blocks[i].instructions->instrs[j].lineno = lineno+count;
			count += blocks[i].instructions->instrs[j].nlines;
		}
		blocks[i].lineno = lineno; 
		blocks[i].nlines = count; 
		lineno += blocks[i].nlines;
	}
	return lineno;
}

void findBasicBlocks(Buffer *bin, int *leaders, int nleaders, BasicBlock **outblocks, int *nblocks, int **invalid, int *ninvalid, Labels *labels) {
	BasicBlock *blocks = NULL;
	int blockCount = 0;
	int blockCapacity = 0;
	
	int *invalidAddrs = NULL;
	int invalidCount = 0;
	int invalidCapacity = 0;

	int endAddr = bufferEndAddress(bin);
	
	// Track which addresses are block leaders
	bool *isLeader = (bool*)calloc(endAddr, sizeof(bool));
	if (!isLeader) return;
	
	// Track visited addresses to avoid infinite loops
	bool *visited = (bool*)calloc(endAddr, sizeof(bool));
	if (!visited) {
		free(isLeader);
		return;
	}
	
	// Stack for addresses to process
	int *stack = (int*)malloc(endAddr * sizeof(int));
	if (!stack) {
		free(isLeader);
		free(visited);
		return;
	}
	int stackTop = 0;
	
	if (leaders == NULL)  {
		// Start at address 0
		isLeader[0] = true;
		stack[stackTop++] = 0;
	} else {
		for(int i = 0; i < nleaders; i++) {
			isLeader[leaders[i]] = true;
			stack[stackTop++] = leaders[i];
		}
	}
	
	// First pass: find all reachable code and mark leaders
	while (stackTop > 0) {
		int addr = stack[--stackTop];
if (addr > 0x00f00000) {
	assert(addr > 0x00f00000);
}
		if (addr >= endAddr || visited[addr]) continue;
		struct Instruction inst;
		memset(&inst, 0, sizeof(inst));
		if (!disasmone(bin, addr, &inst, labels)) {
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
			if (inst.targetAddress && inst.targetAddress < endAddr) {
				isLeader[inst.targetAddress] = true;
				if (!visited[inst.targetAddress]) {
					stack[stackTop++] = inst.targetAddress;
				}
			}

			// Instruction after branch is a leader (for conditional branches, not for subroutine returns)
			if (inst.isBranch && (nextAddr < endAddr)) {
				isLeader[nextAddr] = true;
				if (!visited[nextAddr]) {
					stack[stackTop++] = nextAddr;
				}
			}
		} else if (inst.isRet) {
			// Don't follow after return
		} else {
			// Continue to next instruction
			if (nextAddr < endAddr) {
				if (!visited[nextAddr]) {
					stack[stackTop++] = nextAddr;
				}
			}
		}
	}
	
	// Second pass: build basic blocks
	for (int addr = 0; addr < endAddr; addr++) {
		if (!isLeader[addr] || !visited[addr]) continue;
		
		int blockStart = addr;
		int currentAddr = addr;
		int ninstr = 0;
		IList *ilist = newIList();

		// Find end of basic block
		while (currentAddr < endAddr && visited[currentAddr]) {
			struct Instruction inst;
			if (!disasmone(bin, currentAddr, &inst, labels)) {
				break;
			}
			appendInstruction(ilist, inst);
			
			ninstr++;
			int nextAddr = currentAddr + inst.nbytes;
			
			// Block ends if:
			// 1. Next instruction is a leader
			// 2. This is a branch or return
			// 3. We reach end of binary
			if (nextAddr >= endAddr || 
				(nextAddr < endAddr && isLeader[nextAddr]) ||
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
				blocks[blockCount].instructions = ilist;
				blocks[blockCount].nlines = 1;
				blockCount++;
				break;
			}
			
			currentAddr = nextAddr;
		}
	}
	
	// Sort blocks by start address

	int len = blockCount;
	for (int i = 1; i < len; i ++) {
		BasicBlock b = blocks[i];
		int startAddr = b.begin;
		int j = i - 1;
		
		while (j >= 0 && blocks[j].begin > startAddr) {
			blocks[j + 1] = blocks[j];
			j -= 1;
		}
		
		blocks[j + 1] = b;
	}

	// Find data sections - they lie between basic blocks; we insert them.
	// We construct data sections from Instruction objects tagged as data.  
	// We assign aligned 16 byte chunks to new lines.  These can get split when labeling sub-parts.
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
				blocks[j] = blocks[j-1];
			}
			// Build the data block
			blocks[i].begin = blocks[i].end;
			blocks[i].end = blocks[i+1].begin;
			blocks[i].ninstr = 0; //((blocks[i].end - blocks[i].begin)+7) % 8; // we will present at most 16 bytes per line; instrs count in pairs
			blocks[i].isdata = true;
			// Build the "instructions"
			char asm[512];
			int outbuf[16];
			int start = blocks[i].begin;
			int end = blocks[i].end;
			int endSection = bufferSectionByAddr(bin, start);
			end = MIN(blocks[i].end, bin->sections[endSection]._baseaddress + bin->sections[endSection]._len);
			int address = start;
			asm[0] = 0;
			bufferSeek(bin, blocks[i].begin);
			blocks[i].instructions = newIList();
			for(int addr = blocks[i].begin; addr < blocks[i].end; addr++) {
				// print in linesof 16 bytes
				int linestartaddr = addr;
				for(int j = (address & ~0xf); j < ((end + 0xf)&~0xf); j++) {
					if (j < start || j >= end) {
						strcat(asm, " ");
						outbuf[j%16] = -1;
					} else {
						int byte = bufferGetAt(bin, address++);
						outbuf[j%16] = byte;
						 if (isprint(byte)) {
							char s[2];
							s[0] = byte; s[1] = 0;
							strcat(asm, s);
						} else
							strcat(asm, ".");
					}

					if ((j & 0xf) == 15) {
						strcat(asm, "\t");
						Instruction instr;
						memset(&instr, 0, sizeof(instr));
						instr.nbytes = 0;
						for(int k=0;k<16;k++) {
							if (outbuf[k] == -1) strcat(asm, "   ");
							else {
								char obuf[8];
								sprintf(obuf, "%02x ", outbuf[k]);			
								strcat(asm, obuf);
								instr.nbytes++;
								addr++;
							}
						}
						// Add a line to the last inst of a data block.
							
						instr.asm = strdup(asm); asm[0] = 0;
						instr.address = linestartaddr;
						instr.isdata = 1;
						instr.nlines = 1; linestartaddr = j +1;
						if (j > end) {
							instr.nlines++;
							blocks[i].nlines++;
						}
						blocks[i].nlines++;
						appendInstruction(blocks[i].instructions, instr);
					}
				}
			}
		}
	}
			

	// Count lines

	countlines(bin, blocks, blockCount);
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

		if (line >= blocks[m].lineno  && line < blocks[m].lineno+blocks[m].nlines)
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
