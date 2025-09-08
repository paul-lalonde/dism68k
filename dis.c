#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ncurses.h>
#include <errno.h>
#include "dat.h"

#define ROWWIDTH 16 // Hex displays 16 bytes per row.  Suck it.
#define ROWPREFIX 10

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

WINDOW *hex, *dispad, *cmd;

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
}
void hexstandout(int pos, int on) {
	stylebyte(pos, on, A_STANDOUT);
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
Message("target pos %0x at (r,c): %d, %d; hy = %d; nlines = %d", pos, r, c, hy, nlines);
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

void markasdata(int begin, int end) {
	Unimplemented("markasdata");
}

void interact(Buffer *buf) {
	state.buf = buf;
	noecho();
	start_color();
	initColors();
	curs_set(2);
	clear();
	refresh();

	// 01234567: 0123 4567  0123 4567  0123 4567  0123 4567 
	int hexwidth = 8 + 2 + 4 * 11;
//	int diswidth = COLS - hexwidth;
	hex = newwin(LINES-1, hexwidth, 0, 0);
	box(hex,0,0);
//	dis = newwin(LINES-1, diswidth, 0, hexwidth);
//	box(dis,0,0);
	cmd = newwin(1, COLS, LINES-1, 0);
	box(cmd,0,0);
	
	wprintw(hex, "Hex output");
	scrollok(hex, 1);
	wmove(hex, 0, 0);
	wrefresh(hex);
//	wprintw(dis, "Disassembly");
//	wmove(dis, 0, 0);
//	wrefresh(dis);
	wprintw(cmd, ":read %d bytes", buf->len);
	wmove(cmd, 0, 0);
	wrefresh(cmd);

	// Fill the first screen
	attron(COLOR_PAIR(NORMALMODE));
	fill(buf->bytes, buf->len, 0, 0, LINES, 0, NORMALMODE, NORMALMODE);
	
	hexmoveselection(0,0);

	wrefresh(hex);

	// Show the disassembly
	prefresh(dispad, 0, 0, 0, hexwidth, LINES-1, COLS-1);

	int repeats = 0;
	int hascount = 0;
	int oldoffset = 0;
	int hexmode = 0;

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
		if (hascount == 0 && strchr("hjkl", ch)) 
			repeats = 1;
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
		case '/': // Search
				Unimplemented("Search");
				break;
		case ':':
				Unimplemented("Command");
				break;

		case 'h':
				oldoffset = state.offset;
				state.offset-=repeats;
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
				state.offset+=repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
		case 'g':
				oldoffset = state.offset;
				state.offset = ROWWIDTH * repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
		case 'G': 
				oldoffset = state.offset;
				if (hascount==0) repeats = state.buf->len-1;
				state.offset = repeats;
				if (state.offset > state.buf->len) state.offset = state.buf->len;
				hexmoveselection(oldoffset, state.offset);
				break;
		}
		repeats = 0;
		hascount = 0;
	}

}

int main(void)
{	
	extern void loadanddis(Buffer *);

	// Read our file
	FILE *fp = fopen("W2SYS.BIN", "r");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file\n");
		exit(-1);
	}
	Buffer buf;

	readall(fp, &buf.bytes, &buf.len);

	loadanddis(&buf);

	initscr();			/* Start curses mode 		  */
	cbreak();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);

	// Prep the pad for disassembly
	dispad = newpad(listing.usedlines, 80);
	for(int i=0; i < listing.usedlines; i++) {
		wprintw(dispad, "%s", listing.lines[i]->asm);
	}

	buf.curptr = buf.bytes;
	interact(&buf);

	endwin();			/* End curses mode		  */
	return 0;
}
