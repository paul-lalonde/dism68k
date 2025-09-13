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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "dat.h"

void findBasicBlocks(Buffer *bin, int **outblocks, int *nblocks, int **invalid, int *ninvalid) {
    int *blocks = NULL;
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
        
        struct Instruction inst;
        if (!disasmone(bin, addr, &inst)) {
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
        
        // Find end of basic block
        while (currentAddr < bin->len && visited[currentAddr]) {
            struct Instruction inst;
            if (!disasmone(bin, currentAddr, &inst)) {
                break;
            }
            
            int nextAddr = currentAddr + inst.nbytes;
            
            // Block ends if:
            // 1. Next instruction is a leader
            // 2. This is a branch or return
            // 3. We reach end of binary
            if (nextAddr >= bin->len || 
                (nextAddr < bin->len && isLeader[nextAddr]) ||
                inst.isBranch || inst.isJump || inst.isRet) {
                
                // Add block
                if (blockCount * 2 >= blockCapacity) {
                    blockCapacity = blockCapacity ? blockCapacity * 2 : 32;
                    blocks = realloc(blocks, sizeof(int) * blockCapacity);
                    if (!blocks) break;
                }
                
                blocks[blockCount * 2] = blockStart;
                blocks[blockCount * 2 + 1] = currentAddr + inst.nbytes - 1;
                blockCount++;
                break;
            }
            
            currentAddr = nextAddr;
        }
    }
    
    // Sort blocks by start address
 //   if (blocks && blockCount > 1) {
 //       insertionSort(blocks, blockCount * 2);
 //   }
    int len = blockCount *2;
    for (int i = 2; i < len; i += 2) {
        int startAddr = blocks[i];
        int endAddr = blocks[i + 1];
        int j = i - 2;
        
        while (j >= 0 && blocks[j] > startAddr) {
            blocks[j + 2] = blocks[j];
            blocks[j + 3] = blocks[j + 1];
            j -= 2;
        }
        
        blocks[j + 2] = startAddr;
        blocks[j + 3] = endAddr;
    }
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

void findDataRegions(int *basicblocks, int numblocks, int **retpairs, int *retcount) {
    *retpairs = NULL;
    *retcount = 0;
    
    if (!basicblocks || numblocks == 0) return;
    
    int *regions = NULL;
    int regionCount = 0;
    int regionCapacity = 0;
    
    int lastEnd = -1;
    
    for (int i = 0; i < numblocks; i += 2) {
        int blockStart = basicblocks[i];
        int blockEnd = basicblocks[i + 1];
        
        // Check for gap between last block end and current block start
        if (lastEnd >= 0 && lastEnd + 1 < blockStart) {
            // Found a data region
            if (regionCount * 2 >= regionCapacity) {
                regionCapacity = regionCapacity ? regionCapacity * 2 : 16;
                regions = realloc(regions, sizeof(int) * regionCapacity);
                if (!regions) return;
            }
            
            regions[regionCount * 2] = lastEnd + 1;
            regions[regionCount * 2 + 1] = blockStart - 1;
            regionCount++;
        }
        
        lastEnd = blockEnd;
    }
    
    *retpairs = regions;
    *retcount = regionCount * 2;
}
