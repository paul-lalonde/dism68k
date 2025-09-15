#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ncurses.h>
#include <errno.h>
#include <unistd.h>
#include "dat.h"

#define ROWWIDTH 16 // Hex displays 16 bytes per row.  Suck it.
#define ROWPREFIX 10

int hexwidth;

/* Size of each input chunk to be
   read and allocate for. */
#ifndef  READALL_CHUNK
#define  READALL_CHUNK  262144
#endif

#define  READALL_OK          0  /* Success */
#define  READALL_INVALID    -1  /* Invalid parameters */
#define  READALL_ERROR      -2  /* Stream error */
#define  READALL_TOOMUCH    -3  /* Too much input */
#define  READALL_NOMEM      -4  /* Out of memory */

/* This function returns one of the READALL_ constants above.
   If the return value is zero == READALL_OK, then:
     (*dataptr) points to a dynamically allocated buffer, with
     (*sizeptr) chars read from the file.
     The buffer is allocated for one extra char, which is NUL,
     and automatically appended after the data.
   Initial values of (*dataptr) and (*sizeptr) are ignored.
*/
int readall(FILE *in, unsigned char **dataptr, size_t *sizeptr)
{
    unsigned char  *data = NULL, *temp;
    size_t size = 0;
    size_t used = 0;
    size_t n;

    /* None of the parameters can be NULL. */
    if (in == NULL || dataptr == NULL || sizeptr == NULL)
        return READALL_INVALID;

    /* A read error already occurred? */
    if (ferror(in))
        return READALL_ERROR;

    while (1) {

        if (used + READALL_CHUNK + 1 > size) {
            size = used + READALL_CHUNK + 1;

            /* Overflow check. Some ANSI C compilers
               may optimize this away, though. */
            if (size <= used) {
                free(data);
                return READALL_TOOMUCH;
            }

            temp = realloc(data, size);
            if (temp == NULL) {
                free(data);
                return READALL_NOMEM;
            }
            data = temp;
        }

        n = fread(data + used, 1, READALL_CHUNK, in);
        if (n == 0)
            break;

        used += n;
    }

    if (ferror(in)) {
        free(data);
        return READALL_ERROR;
    }

    temp = realloc(data, used + 1);
    if (temp == NULL) {
        free(data);
        return READALL_NOMEM;
    }
    data = temp;
    data[used] = '\0';

    *dataptr = data;
    *sizeptr = used;

    return READALL_OK;
}

WINDOW *hex, *diswin, *cmd;

void Message(char *s, ...) {
	char *buf; 
	va_list args;
 	va_start (args, s);
  	vasprintf (&buf, s, args);
	mvwprintw(cmd, 0, 0, "%s", buf);
	wrefresh(cmd);
	va_end (args);
	free(buf);
}
	
void Unimplemented(char *s) {
	char buf[512];
	sprintf(buf, "Unimplemented: %s\n", s);
	Message(s);
}

// Fill 
void fill(unsigned char *buf, int len, int pos, int top, int bottom, attr_t attrs, int addrcolor, int fillcolor) {
	attr_t oattrs;
	short color;
	scrollok(hex, 0);
	wattr_get(hex, &oattrs, &color, NULL);
	for(int i = top ; i < bottom; i++) {
		wattr_set(hex, oattrs, addrcolor, NULL);
		mvwprintw(hex, i, 0, "%08x: ", pos);
		for (int j = 0; j < ROWWIDTH/2; j++) {
			wattr_set(hex, attrs, fillcolor, NULL);
			wprintw(hex, "%02x", buf[pos++]);
			if (pos >= len) { goto outofdata; }
			wprintw(hex, "%02x ", buf[pos++]);
			if (pos >= len) { goto outofdata; }
			if (j & 1) wprintw(hex, " "); 
		}
	}
	outofdata:
	scrollok(hex, 1);
	wattr_set(hex, oattrs, color, NULL);
}

typedef struct {
	int offset;
	int windowoffset;
	int datamode; // When true, you're sweeping out data until you hit 'd' again.
	int dmstartoffset;
	Buffer *buf;
	Labels *labels;
	BasicBlock *blocks;
	int nblocks;

	// DISASM
	int line;
	int topline;
	int bblock; // index of first basic block on our screen.
	IList *instructions; // Instructions currently in dispad?
	int *lineAddresses;
	
} State;

static State state;

void offsettoscreen(int pos, int *r, int *c) {
	int remain = pos % ROWWIDTH;
	pos = (pos / ROWWIDTH)*ROWWIDTH;
	pos -= state.windowoffset;
	*r = pos / ROWWIDTH;
	*c = ROWPREFIX + 2 * remain + (remain/2) + remain/4;

}

#define DATAMODE 2
#define NORMALMODE 1
void initColors(void) {
	init_pair(NORMALMODE, COLOR_YELLOW, COLOR_BLACK);
	init_pair(DATAMODE, COLOR_BLACK, COLOR_YELLOW);
}

void stylebyte(int pos, int on, int attr) {
	int r, c;
	chtype ch0, ch1;
	offsettoscreen(pos, &r, &c);
	ch0 = mvwinch(hex, r, c);
	ch1 = mvwinch(hex, r, c+1);
	if (on) {
		mvwaddch(hex, r, c, ch0 | attr);
		mvwaddch(hex, r, c+1, ch1 | attr);
	} else {
		mvwaddch(hex, r, c, ch0 & ~attr);
		mvwaddch(hex, r, c+1, ch1 & ~attr);
	}
}

void blink(int pos, int on) {
	stylebyte(pos, on, A_BLINK);
	stylebyte(pos+1, on, A_BLINK);
}
void hexstandout(int pos, int on) {
	stylebyte(pos, on, A_STANDOUT);
	stylebyte(pos+1, on, A_STANDOUT);
}


int ntab = 0;
void mymvwprint(char *s, int addr, void *d) {
	int row = (int)(uintptr_t)d;
	wmove(diswin, row, 0);
	for(int i=0;i<ntab;i++) wprintw(diswin, "\t");
	wprintw(diswin, "%s", s);
}	

int filldisline(Buffer *bin, int addr, int row, BasicBlock *blocks, int nblocks, Labels *labels) {
	// find the basic block containing addr, disassemble it until we get to addr
	int bb = findAddr(addr, blocks, nblocks);
	Instruction inst;

	if (blocks[bb].isdata) {
		ntab = 2;
		int rval = datadump(bin, blocks[bb].begin, blocks[bb].end, mymvwprint, (void*)(uintptr_t)(row), row+state.topline - blocks[bb].lineno);
		return rval;	
	} 
	int l;
	for(int a = blocks[bb].begin; a < blocks[bb].end; ) {
		disasmone(bin, a, &inst, labels);
		if (inst.address == addr) {
			if ((l = findLabelByAddr(labels, addr)) != -1) {
				mvwprintw(diswin, row, 0, "%08x", addr);
				mvwprintw(diswin, row, 20 - strlen(labels->labels[l].name) - 2 , "%s: ", labels->labels[l].name);
			} else {
				mvwprintw(diswin, row, 0, "%08x ", addr);
			}

			mvwprintw(diswin, row, 20, "%s", inst.asm);
			int nextaddr = addr + inst.nbytes;
			if (nextaddr > blocks[bb].end) { // Past the end of this block.
				if (2*(bb+1) < nblocks) {
					nextaddr = blocks[bb+1].begin; // In the next block.
				} else {
					nextaddr = -2; // Past the end of basic blocks
				}
			}
			return nextaddr;
		} else {
			a += inst.nbytes;
		}
	}
	return -1; // We're probably in a data segment...
}

void hexmoveselection(int oldpos, int pos) {
	int r, c;

	int color = NORMALMODE;
	if (state.datamode) color = DATAMODE;
		

	// Remove old highlight
	hexstandout(oldpos, 0);
	blink(oldpos, 0);

	int hy, hx;
	getmaxyx(hex, hy, hx); // Macro.

	offsettoscreen(pos, &r, &c);
	// Scroll if needed
	if (r <= 0) {
		int nlines = -r ;
		wscrl(hex, -nlines);
		state.windowoffset -= nlines * ROWWIDTH;
		// We now have to fill lines between oldoffset and pos.
		if (nlines > hy) nlines = hy;
		fill(state.buf->bytes, state.buf->len, state.windowoffset, 0, nlines, 0, NORMALMODE, color);
	} else if (r >= hy) {
		int nlines = r - hy + 1; 
		wscrl(hex, nlines);
		state.windowoffset += nlines * ROWWIDTH;
		fill(state.buf->bytes, state.buf->len, state.windowoffset + (hy-nlines)*ROWWIDTH, hy-nlines, hy, 0, NORMALMODE, color);
	}
	
	hexstandout(pos, 1);
	blink(pos, 1);
	wmove(hex, r,c);

	wrefresh(hex);
}

void styleline(int line, int on, int attr) {
	int nrows, ncols;
	getmaxyx(diswin, nrows, ncols);
	int r = line - state.topline;
	if (r > LINES-1 || r < 0) return;
	for (int c = 0; c < ncols; c++) {
		chtype ch0 = mvwinch(diswin, r, c);
		if (on) {
			mvwaddch(diswin, r, c, ch0 | attr);
		} else {
			mvwaddch(diswin, r, c, ch0 & ~attr);
		}
	}
}

void refilldis(Buffer *bin, int addr, BasicBlock *blocks, int nblocks, Labels *labels) {
	wclear(diswin);
	int nextaddr = addr;
	for(int r=0; r<LINES-1;r++) {
		state.lineAddresses[r] = nextaddr;
		nextaddr = filldisline(bin, nextaddr, r, blocks, nblocks, labels);
		assert(nextaddr != -1);
		assert(nextaddr != -2);
	}
}

// Make sure state->line is both disassembled and on screen.
void showLine(State *state, int iline) {
	int line = iline - state->topline;
	int hy, hx;
	getmaxyx(diswin, hy, hx); // Macro.
	if (line > hy || line < 0) {
		wclear(diswin);
		state->line = iline;
		state->topline = iline - hy/2;
		if (state->topline < 0) state->topline = 0;
		refilldis(state->buf, linetoaddr(state->buf, state->blocks, state->nblocks, state->topline), state->blocks, state->nblocks, state->labels);
		wrefresh(diswin);
	} 
}

int offsetToLine(State *state, int offset) {
	int bb = findAddr(offset, state->blocks, state->nblocks);
	int lineno = state->blocks[bb].lineno;
	if (state->blocks[bb].isdata) {
		for(int i=state->blocks[bb].begin; i < state->blocks[bb].end; i++) {
			if (i == offset) return lineno;
			if ((i % 16) == 0) lineno++;
		}
		return lineno;
	}
	int addr;
	for (addr = state->blocks[bb].begin; addr < state->blocks[bb].end; ) {
		if (addr >= offset) return lineno;
		Instruction inst;
		disasmone(state->buf, addr, &inst, state->labels);
		addr += inst.nbytes;
		lineno++;	
	}
	if (addr >= offset) return lineno;
	return -1;
}

// oldline and line are in absolue coordinates; window top is in state.topline
void dismoveselection(Buffer *bin, BasicBlock *blocks, int nblocks, Labels *labels, int oldline, int line) {
	// remove highlight from old line.
	int r = oldline - state.topline;
	styleline(oldline, 0, A_STANDOUT);

	int hy, hx;
	getmaxyx(diswin, hy, hx); // Macro.

	int addr = linetoaddr(bin, blocks, nblocks, line);

	
	// Scroll if needed.  
	r = line - state.topline;
	if (r < 0) {
		int nlines = -r ;
		scrollok(diswin, 1);
		wscrl(diswin, -nlines);
		scrollok(diswin, 0);
		state.topline -= nlines;
		if (nlines > hy) nlines = hy;
		for( int i = 0; i < nlines; i++) {
			state.lineAddresses[i] = addr;
			addr = filldisline(bin, addr, i, blocks, nblocks, labels);
		}
	} else if (r >= hy) { // r is the offset to the new line from topline.
		int nlines = r - hy + 1; 
		scrollok(diswin, 1);
		wscrl(diswin, nlines);
		scrollok(diswin, 0);
		state.topline += nlines;
		for( int i = 0; i < nlines; i++) {
			state.lineAddresses[(hy-nlines) + i] = addr;
			addr = filldisline(bin, addr, (hy-nlines) + i, blocks, nblocks, labels);
		}
	}

	styleline(line, 1, A_STANDOUT);

	wrefresh(diswin);
}


void markasdata(int begin, int end) {
	Unimplemented("markasdata");
}

int exec(char *s) {
	// This should really be a little language, like ed.
	// <range><cmd>/<param>/  
	// But for now it's a hack. And if you want an address on the front in hex, then you lose all commands 0-f
	if (strlen(s) < 1) return FALSE;
	// Might have an address in front.
	char ch;
	char str[128];
	unsigned int addr;
	int matches = sscanf(s, "%x%c%127s", &addr, &ch, str);
	if (matches == 0) {
		addr = state.offset;
		matches = sscanf(s, "%c%s", &ch, str);
		if (matches == 0) { return FALSE; }
	}

	switch(ch) {
	case 'n':
		addLabel(state.labels, str, addr, 0);
		wclear(diswin);
		refilldis(state.buf, linetoaddr(state.buf, state.blocks, state.nblocks, state.topline), state.blocks, state.nblocks, state.labels);
		wrefresh(diswin);
		break;
	case 'p':
		if (str[0] == 0) {
			strcat(s, "labels.txt");
		}
		FILE *fp = fopen(s+1, "w");
		for(int i=0; i < state.labels->len; i++) {
			if (!state.labels->labels[i].generated)
				fprintf(fp,"%0x %s\n", state.labels->labels[i].addr, state.labels->labels[i].name);
		}
		fclose(fp);
		break;
	case 'q':
		return TRUE;
			
	}
	return FALSE;
}


enum EditMode {
	HEXEDITOR = 0,
	DISASMEDITOR
};

void interact(Buffer *buf, Labels *labels, BasicBlock *blocks, int nblocks) {
	state.buf = buf;
	state.labels = labels;
	state.line = 0;
	state.topline = 0;
	state.blocks = blocks;
	state.nblocks = nblocks;

	enum EditMode editmode = HEXEDITOR;
	initscr();			/* Start curses mode 		  */
	cbreak();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	noecho();
	start_color();
	initColors();
	curs_set(0);
	clear();
	refresh();

	// Must happen after ncurses setup.
	state.lineAddresses = malloc(sizeof(int) * (LINES-1));
	memset(state.lineAddresses, 0, sizeof(int)*(LINES-1));

	// 01234567: 0123 4567  0123 4567  0123 4567  0123 4567 
	hexwidth = 8 + 2 + 4 * 11;
	int diswidth = COLS - hexwidth;
	hex = newwin(LINES-1, hexwidth, 0, 0);
	box(hex,0,0);
	diswin = newwin(LINES-1, diswidth, 0, hexwidth);
	scrollok(diswin, 0);
	cmd = newwin(1, COLS, LINES-1, 0);
	box(cmd,0,0);
	
	wprintw(hex, "Hex output");
	scrollok(hex, 1);
	wmove(hex, 0, 0);
	wrefresh(hex);

	refilldis(buf, state.lineAddresses[0], blocks, nblocks, labels);
	wrefresh(diswin);
	wprintw(cmd, ":read %d bytes", buf->len);
	wmove(cmd, 0, 0);
	wrefresh(cmd);

	// Fill the first screen
	attron(COLOR_PAIR(NORMALMODE));
	fill(buf->bytes, buf->len, 0, 0, LINES, 0, NORMALMODE, NORMALMODE);
	
	hexmoveselection(0,0);

	wrefresh(hex);

	// Show the disassembly
	dismoveselection(buf, blocks, nblocks, labels, state.line, state.line);

	int repeats = 0;
	int hascount = 0;
	int oldoffset = 0;
	int hexmode = 0;
	int oldline = 0;

	while (1) {
		char ch = getch();
		if ( ch == 27 ) return;

		char cbuf[512];
		sprintf(cbuf, "                                                        Received keystroke '%c'", ch);
		Message(cbuf);

		if (ch == 'x') { // start a hex numeric string
			hexmode = 1;
			repeats = 0;
			continue;
		}
		if (hexmode) {
			hascount = 1;
			if (ch >= '0' && ch <= '9') {
				repeats = repeats * 16 + (ch - '0');
				continue;
			}
			if (ch >= 'a' && ch <= 'f') {
				repeats = repeats * 16 + (ch - 'a' + 10);
				continue;
			}
			if (ch >= 'A' && ch <= 'F') {
				repeats = repeats * 16 + (ch - 'A' + 10);
				continue;
			}
		} else {
			if (ch >= '0' && ch <= '9') {
				hascount = 1;
				repeats = repeats * 10 + (ch - '0');
				continue;
			}
		}
		hexmode = 0;
		if (hascount == 0 && strchr("hjkl\06\02", ch)) 
			repeats = 1;

		if (ch == '	') {
			editmode ^= 1;
			Message("Changed mode: %d\n", editmode);
		}
		// common mode
		int handled = 1;
		switch(ch) {
		case '/': // Search
				Unimplemented("Search");
				break;
		case ':':
				{
				char buf[128];
				nodelay(cmd, FALSE);
				echo();
				mvwaddch(cmd, 0,0, ':');
				mvwgetnstr(cmd, 0,1, buf, 128);
				noecho();
				if (exec(buf)) return;
				Message("Command: %s", buf);
				}
				break;
		case 'g':
				oldline = state.line;
				oldoffset = state.offset;
				state.offset = repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				int line = offsetToLine(&state, state.offset);
				int r = line - state.topline;
				if (r >= 0 && r < LINES-1) {
					state.line = line;
					dismoveselection(buf, blocks, nblocks, labels, oldline, state.line);
				} else {
					showLine(&state, line);
					state.line = line;
					dismoveselection(buf, blocks, nblocks, labels, state.line, state.line);
				}
				break;
/*		case 'G': 
				oldoffset = state.offset;
				if (hascount==0) repeats = state.buf->len-1;
				state.offset = repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
*/
		default:
				handled = 0;
		}
		if (handled) goto nextloop;
		if (editmode == HEXEDITOR) {
		switch(ch) {
		case 'd':
				Unimplemented("Data mode");
				if (state.datamode != 0) { // Exiting datamode.
					if (state.offset < state.dmstartoffset) {
						markasdata(state.offset, state.dmstartoffset);
					} else {
						markasdata(state.dmstartoffset, state.offset);
					}
					state.datamode = 0;
				} else {
					state.dmstartoffset = state.offset;
					state.datamode = 1;
				}
				break;
		case 'D': // Disassemble starting here, for length
	/*
				if (!hascount) repeats = 0x400; // 1kb default
				listing.usedlines = 0;
				if (instrs) clearIList(instrs);
				else instrs = newIList();
				disasm(buf, state.offset, state.offset+repeats, labels, instrs, 0);
				delwin(dispad);
				dispad = newdisplaypad(buf, blocks, nblocks, labels);
				prefresh(dispad, 0, 0, 0, hexwidth, LINES-1, COLS-1);
*/
				Unimplemented("Disassemble");

				break;

		case 'h':
				oldoffset = state.offset;
				state.offset-=2*repeats;
				if (state.offset < 0) state.offset = 0;
				hexmoveselection(oldoffset, state.offset);
				break;
		case 'j':
				oldoffset = state.offset;
				state.offset+=repeats*ROWWIDTH;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
		case 'k':
				oldoffset = state.offset;
				state.offset -= repeats*ROWWIDTH;
				if (state.offset < 0) state.offset = 0;
				hexmoveselection(oldoffset, state.offset);
				break;
		case 'l':
				oldoffset = state.offset;
				state.offset+= 2*repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
		}
		} else if (editmode == DISASMEDITOR) {
		switch(ch) {
		case 0x07: //^g
				Message("On line %d\n", state.line);
				break;
		case 'r': // refresh
				wclear(diswin);
				refilldis(buf, linetoaddr(state.buf, state.blocks, state.nblocks, state.topline), blocks, nblocks, labels);
				wrefresh(diswin);
				break;

		case 0x06:  //^f
				repeats *= LINES/2;
				// fallthrough
		case 'j':
				oldline = state.line;
				state.line+=repeats;
				if (state.line >= (blocks[nblocks-1].lineno + blocks[nblocks-1].ninstr)) 
					state.line = blocks[nblocks-1].lineno + blocks[nblocks-1].ninstr;
				dismoveselection(buf, blocks, nblocks, labels, oldline, state.line);
				oldoffset = state.offset;
				state.offset = state.lineAddresses[state.line-state.topline];
				hexmoveselection(oldoffset, state.offset);
				break;
		case 0x02:  //^b
				repeats *= LINES/2;
				// fallthrough
		case 'k':
				oldline = state.line;
				state.line-=repeats;
				if (state.line < 0) state.line = 0;
				dismoveselection(buf, blocks, nblocks, labels, oldline, state.line);
				oldoffset = state.offset;
				state.offset = state.lineAddresses[state.line-state.topline];
				hexmoveselection(oldoffset, state.offset);
				break;
		}
		
		}
		nextloop:
		repeats = 0;
		hascount = 0;
	}

}

// Only generate labels if there isn't already a label for that address.
// Add them as auto-generated so they don't get saved and restored.
void generateLabels(Labels *l, BasicBlock *blocks, int nblocks) {
	for(int i = 0; i < nblocks; i++) {
		if (findLabelByAddr(l, blocks[i].begin) == -1) {
			char buf[128];
			sprintf(buf, "L%06x", blocks[i].begin);
			addLabel(l, buf, blocks[i].begin, 1);
		}
	}
}

void myfprint(char *s, int addr, void *d) {
	FILE *fp = (FILE *)d;
	for(int i=0;i<ntab;i++) fprintf(fp,"\t");
	fprintf(fp, "%s", s);
	ntab = 4; // Cheesy "use fewer tabs on each start"
}

int main(void)
{	// yes, we need command line parsing now.

	//kill(getpid(), SIGSTOP);
	Labels *labels = newLabels(1);
	state.labels = labels;
	FILE *fp = fopen("labels.txt", "r");
	if (fp != NULL) {
		freadLabels(fp, labels);
		fclose(fp);
	}

	// Read our file
	fp = fopen("W2SYS.BIN", "r");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file\n");
		exit(-1);
	}
	Buffer buf;

	readall(fp, &buf.bytes, &buf.len);
	fclose(fp);
	buf.curptr = buf.bytes;

	// Calculate basic blocks
	BasicBlock *blocks=0;
	int nblocks, *invalid, ninvalid;
	findBasicBlocks(&buf, &blocks, &nblocks, &invalid, &ninvalid);
	generateLabels(labels, blocks, nblocks);
	
	FILE *outfile = fopen("disasm.txt", "w");
	Instruction instr;
	int l;
	for(int i = 0; i < nblocks; i++) {
		char str[128];
		sprintf(str, "\t# Block %d:%06x-%06x: line %d", i, blocks[i].begin, blocks[i].end, blocks[i].lineno); 
		for(int addr = blocks[i].begin; addr < blocks[i].end; ) {
			if ((l = findLabelByAddr(labels, addr)) != -1) {
//				fprintf(outfile, "%08x", addr);
				fprintf(outfile, "%16s: ", labels->labels[l].name);
			} else {
				fprintf(outfile, "%08x\t", addr);
			}
			if (blocks[i].isdata) {
				if (addr == blocks[i].begin) ntab = 2;
				datadump(&buf, blocks[i].begin, blocks[i].end, myfprint, outfile, -1);
				addr = blocks[i].end;
				continue;
			}
			disasmone(&buf, addr, &instr, state.labels);
			fprintf(outfile, "\t\t%s%s\n", instr.asm, str);
			str[0] = 0;
			addr += instr.nbytes;
		}
	}
	fclose(outfile);
	
	buf.curptr = buf.bytes;
	interact(&buf, labels, blocks, nblocks);

	endwin();			/* End curses mode		  */
	return 0;
}
