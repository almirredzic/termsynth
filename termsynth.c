// termsynth version 0.1 - an ALSA based synthesizer for Linux terminal
// Copyright (C) 2014 Almir Redzic (redzicalmir@gmail.com)
// 
// This file is part of termsynth.
// 
// termsynth is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// termsynth is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with termsynth.  If not, see <http://www.gnu.org/licenses/>.

// common
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <dirent.h>
#include <termios.h>
#include <stdarg.h>

// ALSA, DSP
#include <alsa/asoundlib.h>
#include <math.h>

// Performance improvement options
#define INLINEMODULES   1
#define BUFFERSIZE    512

/***********************************************************************************************************************
 UI declaration section 
***********************************************************************************************************************/

#define MAX_WIDTH 250
#define MAX_HEIGHT 60
#define MIN_WIDTH 80
#define MIN_HEIGHT 24

#define TERM_SHOW_CURSOR   "\033[?25h"
#define TERM_HIDE_CURSOR   "\033[?25l"
#define TERM_ALTER_SCREEN  "\033[?47h"
#define TERM_NORMAL_SCREEN "\033[?47l"
#define TERM_PUT_CURSOR    "\033[%d;%dH"    // y, x: (1, 1) - (..., ...)
#define TERM_FCOLOR        "\033[3%dm"      // 0 - 7
#define TERM_BCOLOR        "\033[4%dm"      // 0 - 7
#define TERM_256_FCOLOR    "\033[38;5;%dm"  // 16 - 255
#define TERM_256_BCOLOR    "\033[48;5;%dm"  // 16 - 255
#define TERM_CLEAR         "\033[2J"
#define TERM_BOLD          "\033[1m"
#define TERM_BOLD_OFF      "\033[22m"
#define TERM_UNDERLINE     "\033[4m"
#define TERM_UNDERLINE_OFF "\033[24m"
#define TERM_INVERTED      "\033[7m"
#define TERM_INVERTED_OFF  "\033[27m"
#define TERM_ALL_OFF       "\033[0m"

enum KEY { KEY_UNKNOWN, KEY_ESCAPE, KEY_HOME, KEY_INSERT, KEY_END, KEY_PGUP, KEY_PGDN,
           KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
           KEY_SHIFT_RIGHT, KEY_SHIFT_LEFT,
           KEY_TAB, KEY_BACKSPACE, KEY_SHIFT_TAB, KEY_ENTER
         };

#define LIST_LINES       512
#define LIST_LINE_LEN    512

typedef struct {
    char data[LIST_LINES][LIST_LINE_LEN];
    int buffer_pos;
    int start_row;
    int total;
    int x;
    int y;
    int width;
    int height;
} list;

#define MAXBANK 14000

typedef struct {
    list mainlist;
    list sidelist;
    int showcolor;
    char commandline[LIST_LINE_LEN];
    int winwidth;
    int winheight;
    int totalwinwidth;
    int totalwinheight;
    int showmessages;
    int skiplistshow;
    int quit_program;
    int selected_patch;
    int debug;
    char bank[MAXBANK][128];
    int banksize;
    int selpatch[4];
    int selinput;
} uistruct;

uistruct ui = {
    .mainlist = {
        .buffer_pos = 0, .start_row = LIST_LINES - 1, .total = 0, .x = 1, .y = 1, .width = 4, .height = 4
    },
    .sidelist = {
        .buffer_pos = 0, .start_row = LIST_LINES - 1, .total = 0, .x = 5, .y = 1, .width = 4, .height = 4
    },
    .showcolor = 1, .quit_program = 0, .skiplistshow = 0,
    .showmessages = 1, .selinput = -1, .debug = 0, .selected_patch = 0
};

/***********************************************************************************************************************
 Synthesis declaration section 
***********************************************************************************************************************/

#define GAIN 5000.0
#define WAVEFORM_SAMPLES 2048
#define PATCHNAMESIZE 64
#define RECBUFFERSIZE 13230000 // [44100 * 60 * 5] (5 minutes, around 10 MB/minute for stereo wave-file)
#define MODULES 64
#define WAVES 6
#define VOICES 16 // per patch
#define PATCHES 12

enum MODULETYPES {NOP, OSC, ENV, MIX, MUL, FLT, DLY, SEQ, POL, MOD, CLK, OUT};

int buffer_underrun_count = 0;
snd_seq_t *seq_handle;
snd_pcm_t *playback_handle;
short *buf;

typedef struct {
    int    constant;
    int    patchnr; // used only by OSC to get wave
    int    type;
    int    inputs[8];
    char   input_names[8][32];
    int    ip[6];   // integer parameters
    double fp[33];  // float parameters
    char   line[512];
    char   name[32];
    double midicc;  // scaled to [0, 1], used only by MOD
    double pitchwheel; // scaled to [-1, 1], used only by MOD
} module;

typedef struct {
    int    started;
    int    notenumber;
    int    gate;
    int    active;
    int    onchannel;
    double frequency;
    double scaledfrequency; // (0 - 22050 Hz) -> (0 - 1)
    double invertedfrequency;
    double velocity;
    int    zerocount;
    double moduledata[MODULES][8];
    int    imoduledata[MODULES][4];
    double out[MODULES];
    double *largedata[MODULES]; // dynamically allocated - used only as delay buffer
} voice;

typedef struct {
    double *data;
    int    size;
    int    type;
    int    harmonics;
    int    pulsewidth;
} wave;

typedef struct {
    int    modulesused;
    int    wavesused;
    int    midichannel;
    int    startnote;
    int    endnote;
    int    minvelocity;
    double volume;
    char   name[32];
    wave   waves[WAVES];
    module modules[MODULES];
    voice  voices[VOICES];
} patch;

static patch patches[PATCHES];

static unsigned int Rate;
static double time_delta;
static int record_len[PATCHES];
static int record_started = 0;
short int record_buffer[RECBUFFERSIZE * 2];

#define EXP2TABLESIZE 4000
static double exp2_table[EXP2TABLESIZE];
#define SQRTTABLESIZE 4000
static double sqrt_table[SQRTTABLESIZE];

// functions
void show_patch(int);
void note_on(int, int, int);
void note_off(int, int);
int command_interpreter(char*);

/***********************************************************************************************************************
 UI section 
***********************************************************************************************************************/

/********************************************************************
    returns string length, not counting escape sequences that end
    with 'm' - like colour, bold and underline attributes
********************************************************************/
int strlen_noescape(const char *s) {
    int count = 0, skip = 0;
    const char *t = s;
    while(*t != '\0') {
        if (*t == 27) skip = 1;
        if (*t == 'm' && skip == 1) skip = 2;
        if (skip == 0) count++;
        if (skip == 2) skip = 0;
        t++;
    }
    return count;
}

/********************************************************************
    truncates line to a specified size, not counting escape
    sequences, because they do not affect the length of visible text
********************************************************************/
char* trunc_line_noescape(char *out, const char *in, int maxlen) {
    int count = 0, outpos = 0, skip = 0;
    const char *t = in;
    while(*t != '\0') {
        if (*t == 27) skip = 1;
        if (*t == 'm' && skip == 1) skip = 2;
        if (count < maxlen) {
            out[outpos++] = *t;
            if (skip == 0) count++;
        }
        if (skip == 2) skip = 0;
        t++;
    }
    out[outpos] = '\0';
    return out;
}

void goto_xy(int x, int y) {
    int xoffset = 0;
    int yoffset = 0;
    if (ui.totalwinheight > MAX_HEIGHT) yoffset = (ui.totalwinheight - MAX_HEIGHT) / 2;
    if (ui.totalwinwidth > MAX_WIDTH) xoffset = (ui.totalwinwidth - MAX_WIDTH) / 2;
    printf(TERM_PUT_CURSOR, y + yoffset, x + xoffset);
}

int show_list(list *l, int key) {
    if (ui.skiplistshow) return 0;
    printf(TERM_HIDE_CURSOR);
    char tempstr[LIST_LINE_LEN];
    if (key == KEY_DOWN && l->start_row < LIST_LINES - 1) l->start_row += 1;
    if (key == KEY_UP && l->start_row > LIST_LINES - l->total) l->start_row -= 1;
    if (key == KEY_HOME) l->start_row = LIST_LINES - l->total;
    if (key == KEY_END) l->start_row = LIST_LINES - 1;
    if (key == KEY_PGUP) {
        l->start_row -= l->height;
        if (l->start_row < LIST_LINES - l->total) l->start_row = LIST_LINES - l->total;
    }
    if (key == KEY_PGDN) {
        l->start_row += l->height;
        if (l->start_row >= LIST_LINES) l->start_row = LIST_LINES - 1;
    }
    if (l->start_row > LIST_LINES - l->height) l->start_row = LIST_LINES - l->height;
    int i;
    for (i = 0; i < l->height; i++) {
        int rownum = l->start_row + i;
        goto_xy(l->x, l->y + i);
        trunc_line_noescape(tempstr, l->data[(l->buffer_pos + rownum) % LIST_LINES], l->width);
        int len_diff = strlen(tempstr) - strlen_noescape(tempstr);
        printf(TERM_ALL_OFF "%-*s", l->width + len_diff, trunc_line_noescape(tempstr, l->data[(l->buffer_pos + rownum) % LIST_LINES], l->width));
    }
    return 0;
}

void show_prompt() {
    goto_xy(1, ui.winheight);
    printf(TERM_ALL_OFF);
    if (ui.selinput > -1) printf(TERM_INVERTED);
    printf("%-*.*s", ui.winwidth, ui.winwidth, "");
    goto_xy(1, ui.winheight);
    printf(":%-.*s", ui.winwidth - 1, ui.commandline);
    printf(TERM_SHOW_CURSOR);
}

void handle_winch(int sig) {
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    ui.totalwinwidth = w.ws_col;
    ui.totalwinheight = w.ws_row;
    if (ui.totalwinwidth > MAX_WIDTH) ui.winwidth = MAX_WIDTH; else ui.winwidth = ui.totalwinwidth;
    if (ui.totalwinheight > MAX_HEIGHT) ui.winheight = MAX_HEIGHT; else ui.winheight = ui.totalwinheight;
    printf(TERM_ALL_OFF);
    printf(TERM_CLEAR);
    ui.mainlist.width = 2 * ui.winwidth / 3;
    ui.sidelist.x = ui.mainlist.x + ui.mainlist.width + 2;
    ui.sidelist.width = ui.winwidth - ui.mainlist.width - 2;
    ui.mainlist.height = ui.winheight - 1;
    ui.sidelist.height = ui.winheight - 1;
    ui.mainlist.start_row = LIST_LINES - 1;
    ui.sidelist.start_row = LIST_LINES - 1;
    show_patch(ui.selected_patch);
    show_list(&ui.sidelist, 255);
    show_prompt();
}

char* syntax_color(char *out, const char *in, int colornr) {
    int i = 0, j = 0;
    char color[40];
    sprintf(color, TERM_256_FCOLOR, colornr);
    out[0] = '\0';
    while (in[i]) {
        out[j] = in[i];
        j++;
        out[j] = '\0';
        if ((in[i + 1] == ' ' && in[i] != ' ') || in[i + 1] == '\0') {
            strcat(out, TERM_ALL_OFF);
            j += strlen(TERM_ALL_OFF);
        }
        if (strlen(&in[i]) > 4)
        if ((strncmp(&in[i + 1], "osc ", 4) == 0) || (strncmp(&in[i + 1], "env ", 4) == 0) || (strncmp(&in[i + 1], "mix ", 4) == 0) ||
            (strncmp(&in[i + 1], "mul ", 4) == 0) || (strncmp(&in[i + 1], "flt ", 4) == 0) || (strncmp(&in[i + 1], "dly ", 4) == 0) ||
            (strncmp(&in[i + 1], "seq ", 4) == 0) || (strncmp(&in[i + 1], "pol ", 4) == 0) || (strncmp(&in[i + 1], "mod ", 4) == 0) ||
            (strncmp(&in[i + 1], "clk ", 4) == 0) || (strncmp(&in[i + 1], "out ", 4) == 0)) {
            strcat(out, TERM_BOLD);
            j += strlen(TERM_BOLD);
        }
        if (in[i] == '=') {
            if (ui.showcolor) {
                strcat(out, color);
                j += strlen(color);
            }
            if ((in[i - 4] == 'n' && in[i - 3] == 'a' && in[i - 2] == 'm' && in[i - 1] == 'e') || in[i - 1] == ':') {
                strcat(out, TERM_BOLD);
                j += strlen(TERM_BOLD);
            }
        }
        i++;
    }
    return out;
}

void add_line_to_list(list *l, const char *format, ...) {
    char message[LIST_LINE_LEN];
    va_list va;
    va_start(va, format);
    vsprintf(message, format, va);
    if (ui.debug == 0 && strncmp(message, "DEBUG:", 6) == 0) return;
    strcpy(l->data[l->buffer_pos], message);
    if (l->total < LIST_LINES) l->total++;
    l->buffer_pos++;
    l->buffer_pos = l->buffer_pos % LIST_LINES;
    show_list(l, 255);
}

void add_long_line_to_list(list *l, const char *line, int colornr, int breakoffset) {
    char out[LIST_LINE_LEN];
    char colored[LIST_LINE_LEN];
    const char *t = line;
    int outpos = 0;
    while(*t != '\0') {
        out[outpos++] = *t;
        if (outpos > l->width) {
            while (*t != ' ') {
                t--;
                outpos--;
            }
            out[outpos] = '\0';
            add_line_to_list(l, "%s", syntax_color(colored, out, colornr));
            out[0] = '\0';
            int i;
            for (i = 0; i < breakoffset; i++) strcat(out, " ");
            outpos = breakoffset;
        }
        t++;
    }
    out[outpos] = '\0';
    add_line_to_list(l, "%s", syntax_color(colored, out, colornr));
}

void show_patch(int patchnr) {
    ui.skiplistshow = 1;
    ui.mainlist.total = 0; // clear list
    ui.mainlist.buffer_pos = 0;
    if (ui.showcolor)
        add_line_to_list(&ui.mainlist, TERM_INVERTED TERM_256_FCOLOR "Patch %d - %s", patchnr, patchnr, patches[patchnr].name);
    else
        add_line_to_list(&ui.mainlist, TERM_INVERTED "Patch %d - %s", patchnr, patches[patchnr].name);
    add_line_to_list(&ui.mainlist, "midichannel:%d startnote:%d endnote:%d volume:%2.4f minvelocity:%d",
        patches[patchnr].midichannel, patches[patchnr].startnote, patches[patchnr].endnote, patches[patchnr].volume, patches[patchnr].minvelocity);
    int i;
    char numerated[LIST_LINE_LEN];
    for (i = 1; i < patches[patchnr].modulesused; i++) {
        sprintf(numerated, "%2d %s", i, patches[patchnr].modules[i].line);
        add_long_line_to_list(&ui.mainlist, numerated, patchnr, 7);
    }
    int linestoadd = ui.mainlist.height - ui.mainlist.total;
    for (i = 0; i < linestoadd; i++) add_line_to_list(&ui.mainlist, "");
    ui.skiplistshow = 0;
    ui.mainlist.start_row = LIST_LINES - ui.mainlist.total;
    show_list(&ui.mainlist, 255);
}

void handle_bank_keys(int key) {
    if (key == KEY_TAB) {
        if (ui.selinput < 0) strcpy(ui.commandline, "read ");
        if (ui.selinput < 3) ui.selinput += 1; else return;
        if (ui.commandline[strlen(ui.commandline) - 1] != ' ') strcat(ui.commandline, " ");
    }
    if (key == KEY_BACKSPACE) {
        if (ui.selinput > -1) ui.selinput -= 1;
        if (ui.selinput < 0) ui.commandline[0] = '\0';
        else *(strrchr(ui.commandline, ' ')) = '\0';
    }
    if (key == KEY_RIGHT) {
        ui.selpatch[ui.selinput] = (ui.selpatch[ui.selinput] + 1) % ui.banksize;
    }
    if (key == KEY_LEFT) {
        ui.selpatch[ui.selinput] -= 1;
        if (ui.selpatch[ui.selinput] < 0) ui.selpatch[ui.selinput] += ui.banksize;
    }
    if (key == KEY_DOWN || key == KEY_UP) {
        int start = ui.selpatch[ui.selinput];
        char startbank[100];
        char destbank[100];
        strcpy(startbank, ui.bank[start]);
        *(strrchr(startbank, '/')) = '\0';
        while (1) {
            if (key == KEY_DOWN) ui.selpatch[ui.selinput] = (ui.selpatch[ui.selinput] + 1) % ui.banksize;
            else {
                ui.selpatch[ui.selinput] -= 1;
                if (ui.selpatch[ui.selinput] < 0) ui.selpatch[ui.selinput] += ui.banksize;
            }
            if (ui.selpatch[ui.selinput] == start) break; // full circle - only one bank
            strcpy(destbank, ui.bank[ui.selpatch[ui.selinput]]);
            *(strrchr(destbank, '/')) = '\0';
            if (strcmp(startbank, destbank) != 0) break;
        }
    }
    if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_DOWN || key == KEY_UP || key == KEY_TAB) {
        *(strrchr(ui.commandline, ' ') + 1) = '\0';
        strcat(ui.commandline, ui.bank[ui.selpatch[ui.selinput]]);
    }
    show_prompt();
}

void *io_thread_function() {
    struct termios old_tio, new_tio;
    int key;
    tcgetattr(STDIN_FILENO, &old_tio); // get the terminal settings for STDIN
    new_tio = old_tio; // keep the old setting to restore them at the end
    new_tio.c_lflag &= (~ICANON & ~ECHO); // disable canonical mode (buffered I/O) and local echo
    new_tio.c_cc[VTIME] = 0;
    new_tio.c_cc[VMIN] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio); // set the new settings immediately
    unsigned char c[32];
    int normalinput = 0;
    do { 
        usleep(10000);
        int chcount = 0;
        memset(c, 0, 32 * sizeof(unsigned char));
        do {
            c[chcount] = getchar();
            chcount += 1;
        } while (c[chcount - 1] != 255);
        if (c[0] == 255) continue;
        add_line_to_list(&ui.sidelist, "DEBUG: key: (%d) %d %d %d %d %d %d %d %d", chcount, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
        key = -1;
        if (c[0] == 10) key = KEY_ENTER;
        if (c[0] == 9) key = KEY_TAB;
        if (c[0] == 127) key = KEY_BACKSPACE;
        if (c[0] == 27 && c[1] == 255) key = KEY_ESCAPE;
        if (c[0] == 27 && c[1] == 91 && c[2] == 72) key = KEY_HOME;
        if (c[0] == 27 && c[1] == 91 && c[2] == 70) key = KEY_END;
        if (c[0] == 27 && c[1] == 91 && c[2] == 50 && c[3] == 126) key = KEY_INSERT;
        if (c[0] == 27 && c[1] == 91 && c[2] == 53 && c[3] == 126) key = KEY_PGUP;
        if (c[0] == 27 && c[1] == 91 && c[2] == 54 && c[3] == 126) key = KEY_PGDN;
        if (c[0] == 27 && c[1] == 91 && c[2] == 65) key = KEY_UP;
        if (c[0] == 27 && c[1] == 91 && c[2] == 66) key = KEY_DOWN;
        if (c[0] == 27 && c[1] == 91 && c[2] == 67) key = KEY_RIGHT;
        if (c[0] == 27 && c[1] == 91 && c[2] == 68) key = KEY_LEFT;
        if (c[0] == 27 && c[1] == 91 && c[2] == 90) key = KEY_SHIFT_TAB;
        if (c[0] == 27 && c[1] == 91 && c[2] == 49 && c[3] == 59 && c[4] == 50 && c[5] == 67) key = KEY_SHIFT_RIGHT;
        if (c[0] == 27 && c[1] == 91 && c[2] == 49 && c[3] == 59 && c[4] == 50 && c[5] == 68) key = KEY_SHIFT_LEFT;
        if (key == -1 && c[0] == 27) key = KEY_UNKNOWN;

        if (key == KEY_TAB) handle_bank_keys(key);
        if ((ui.selinput > -1) && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || key == KEY_BACKSPACE)) handle_bank_keys(key);

        if (ui.selinput < 0 && (key == KEY_DOWN || key == KEY_UP || key == KEY_HOME || key == KEY_END
            || key == KEY_PGDN || key == KEY_PGUP)) show_list(&ui.mainlist, key);
        if (ui.selinput < 0 && key == KEY_LEFT && ui.selected_patch > 0) {
            ui.selected_patch -= 1;
            show_patch(ui.selected_patch);
        }
        if (ui.selinput < 0 && key == KEY_RIGHT && ui.selected_patch < PATCHES - 1) {
            ui.selected_patch += 1;
            show_patch(ui.selected_patch);
        }
        if (key == KEY_BACKSPACE && ui.selinput < 0) {
            int len = strlen(ui.commandline);
            if (len > 0) ui.commandline[len - 1] = '\0';
        }
        if (key == -1 && ui.selinput < 0) {
            int len = strlen(ui.commandline);
            if (len < LIST_LINE_LEN) {
                ui.commandline[len] = c[0];
                add_line_to_list(&ui.sidelist, "DEBUG: chr: %d", c[0]);
                ui.commandline[len + 1] = '\0';
            }
        }
        if (key == KEY_ENTER) {
            if (strcmp(ui.commandline, "quit") == 0) break;
            add_line_to_list(&ui.sidelist, "%s", ui.commandline);
            command_interpreter(ui.commandline);
            ui.commandline[0] = '\0';
            ui.selinput = -1;
        }
        if (ui.selinput < 0 && (key == -1 || key == KEY_BACKSPACE || key == KEY_ENTER)) show_prompt();
    } while (1);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio); // restore terminal settings
    ui.quit_program = 1;
    return NULL;
}

/***********************************************************************************************************************
 Waveform and wave-file handling section 
***********************************************************************************************************************/

int write_stereo_wav_file(char *fileName, int rec_len[PATCHES]) {
    static char RIFF[4] = { 82, 73, 70, 70 }; // RIFF
    static char STEREO_TEMPLATE[32] = { 87, 65, 86, 69, 102, 109, 116, 32,
                                       16, 0, 0, 0, 1, 0, 2, 0, 68, 172, 0, 0, 16,
                                       177, 2, 0, 4, 0, 16, 0, 100, 97, 116, 97 };
    FILE *file;
    int i, size = 0;
    for (i = 0; i < PATCHES; i++) if (rec_len[i] > size) size = rec_len[i];
    if ((file = fopen(fileName, "wb")) == NULL) {
        add_line_to_list(&ui.sidelist, "%s", "Error: Cannot write file");
        return -1;
    }
    fwrite(RIFF, 4, 1, file);
    int t = size * 4 + 36;
    fwrite(&t, 4, 1, file);
    fwrite(STEREO_TEMPLATE, 32, 1, file);
    t -= 36;
    fwrite(&t, 4, 1, file);
    fwrite(record_buffer, size * 4, 1, file); // 4 = 2 channels * 2 bytes
    fclose(file);
    return 0;
}

double* make_wave(int type, int harmonics, int pulsewidth, int size) {
    int i, j;
    double* buffer;
    double f, t = 0, max = 0, min = 0, absmax, r, omega, gibbs_fejer_window;
    double Tp = ((double) pulsewidth / 100.0) * size * time_delta;
    buffer = (double *) malloc(sizeof(double) * size);
    f = (double)Rate / (double)size;
    for (i = 0; i < size; i++) {
        buffer[i] = 0;
        for (j = 1; j <= harmonics; j++) {
            omega = 2 * M_PI * j * f;
            gibbs_fejer_window = (double)(harmonics - j) / (double)harmonics;
            if (type == 1 && j == 1) buffer[i] += sin(omega * t); // sine
            if (type == 2 && (j % 2 != 0)) buffer[i] += sin(omega * t) * (1.0 / j) * gibbs_fejer_window; // square
            if (type == 3) buffer[i] += pow(-1, j + 1) * sin(omega * t) * (1.0 / j) * gibbs_fejer_window; // saw
            if (type == 4)
                buffer[i] += pow(-1, j - 1) * sin(2 * M_PI * (2 * j - 1) * f * t) * (1.0 / pow(2 * j - 1, 2)) * gibbs_fejer_window; // triangle
            if (type == 5)
                buffer[i] += (sin(omega * Tp) * cos(omega * t) + (1.0 - cos(omega * Tp)) * sin(omega * t)) * (1.0 / (j * M_PI)) * gibbs_fejer_window; // pulse
        }
        if (buffer[i] > max) max = buffer[i];
        if (buffer[i] < min) min = buffer[i];
        t += time_delta;
    }
    absmax = (fabs(max) > fabs(min)) ? fabs(max) : fabs(min);
    r = 1.0 / absmax;
    for (i = 0; i < size; i++) {
        if (type < 5)
            buffer[i] *= r;
        else
            buffer[i] = (buffer[i] - max + (max - min) * 0.5) * 2.0 / (max - min);
    }
    return buffer;
}

void clean_waveforms(int patch_nr) {
    int i;
    for (i = 0; i < WAVES; i++) {
        if (patches[patch_nr].waves[i].data != NULL) free(patches[patch_nr].waves[i].data);
        patches[patch_nr].waves[i].data = NULL;
    }
}

void clean_all() {
    int i, j, k;
    for (i = 0; i < PATCHES; i++) {
        clean_waveforms(i);
        for (j = 0; j < MODULES; j++)
            for (k = 0; k < VOICES; k++) {
                if (patches[i].voices[k].largedata[j] != NULL)
                    free(patches[i].voices[k].largedata[j]);
                patches[i].voices[k].largedata[j] = NULL;
            }
    }
}

int find_string_in_array(char *ps[], int np, const char *p) {
    int i;
    for (i = 0; i < np; i++)
        if (strcmp(ps[i], p) == 0) return i;
    return -1;
}

char* get_random_option(char *ret) {
    char *tp, *tv;
    char input_copy[100];
    char *options[32];
    int optionsnum = 0;
    strcpy(input_copy, ret);
    tp = strtok(input_copy, "=");
    tv = strtok(NULL, "=");
    options[0] = strtok(tv, "|");
    while (options[optionsnum] != NULL) {
        optionsnum++;
        options[optionsnum] = strtok(NULL, "|");
    }
    int choice = rand() % optionsnum; // [0, optionsnum - 1]
    strcpy(ret, tp);
    strcat(ret, "=");
    strcat(ret, options[choice]);
    return ret;
}

char* get_param_value(char **ps, int np, const char *param_name, char *default_value, char *ret) {
    int i;
    char ts[100];
    char *tp;
    char *tv;
    for (i = 0; i < np; i++) {
        strcpy(ts, ps[i]);
        if (strchr(ts, '=') == NULL) continue;
        tp = strtok(ts, "=\n");
        tv = strtok(NULL, "=\n");
        if (strcmp(tp, param_name) == 0) {
            strcpy(ret, tv);
            return ret;
        }
    }
    strcpy(ret, default_value);
    return ret;
}

double calculate_freq_detune(int semitone, int cent) {
    double out = 1.0;
    out *= pow(2, semitone / 12); // octave detune
    semitone = semitone % 12;
    out *= pow(1.0595, semitone); // semitone detune
    out *= pow(1.0005777895, cent); // cent detune
    return out;
}

int parse_module_definition(patch *ppatch, const char *line, int parsed_module) {
    char *command;
    char *params[32];
    char ret[100];
    int paramsnum = 0;
    char tmpline[LIST_LINE_LEN];
    strcpy(tmpline, line);
    command = strtok(tmpline, " \n");
    if (command == NULL) return -1;;
    while (1) {
        params[paramsnum] = strtok(NULL, " \n");
        if (params[paramsnum] == NULL) break;
        paramsnum++;
    }
    module *pmodule = &patches[ui.selected_patch].modules[parsed_module];
    static char *templates[] = {
        "nop name=_",
        "osc name=_ phase:=# pitch:=# amp:=# detune=1.0 frequency=0.0 type=sine harmonics=64 pulsewidth=50",
        "env name=_ amp:=# t0=0.0 t1=0.01 t2=0.0001 t3=0.0001 t4=0.01 l0=123.456 l1=1.0 l2=123.456 l3=1.0 l4=0.0 shape=natural",
        "mix name=_ crossfade:=# a:=0 b:=0 c:=0 d:=0 e:=0 f:=0",
        "mul name=_ a:=0 b:=0",
        "flt name=_ input:=0 cutoff:=# resonance:=# cutoff=1.0 resonance=0.0 type=lowpass",
        "dly name=_ input:=0 time:=# amount=1.0 feedback=0.0 time=0.01",
        "seq name=_ step:=# default=0.0 steps=1 wrap=yes "
                    "s0=123.456 s1=123.456 s2=123.456 s3=123.456 s4=123.456 s5=123.456 s6=123.456 s7=123.456 s8=123.456 "
                    "s9=123.456 s10=123.456 s11=123.456 s12=123.456 s13=123.456 s14=123.456 s15=123.456",
        "pol name=_ input:=0 a0=0.0 a1=1.0 a2=0.0 a3=0.0 a4=0.0 a5=0.0 a6=0.0 a7=0.0",
        "mod name=_ input:=# amp=1.0 offset=0.0 source=frequency number=1 scale=none",
        "clk name=_ time:=# time=0.0",
        "out name=_ left:=0 right:=0",
        "NOTFOUND"
    };
    
    static char *constants[] = {
        "empty:0", "sine:1", "square:2", "saw:3", "triangle:4", "pulse:5",
        "lowpass:0", "highpass:1", "bandpass:2", "bandstop:3",
        "no:0", "yes:1", "none:0", "exp2:1", "inv:2", "int:3", "abs:4", "natural:0", "linear:1",
        "frequency:1", "velocity:2", "controller:3", "1/frequency:4", "pitchwheel:5", "midinote:6"
    };
    
    char *tparams[32];
    char tline[512];
    int incount = 0, fcount = 0, icount= 0, i, j, k, tparamsnum = 0;
    for (i = 0; i <= 12; i++) if (strncmp(templates[i], command, 3) == 0 && strlen(command) == 3) break;
    if (i > 11) return 0;
    add_line_to_list(&ui.sidelist, "DEBUG: Parsing module:%s", command);
    strcpy(pmodule->line, command);
    for (j = 0; j < paramsnum; j++) {
        if (strchr(params[j], '|') != NULL) get_random_option(params[j]);
        strcat(pmodule->line, " ");
        strcat(pmodule->line, params[j]);
    }
    strcpy(tline, templates[i]);
    strtok(tline, " ");
    while (1) {
        tparams[tparamsnum] = strtok(NULL, " ");
        if (tparams[tparamsnum] == NULL) break;
        tparamsnum++;
    }
    pmodule->type = i;
    add_line_to_list(&ui.sidelist, "DEBUG: Module type is: %d", i);
    pmodule->patchnr = ui.selected_patch;
    for (j = 0; j < tparamsnum; j++) {
        char *tp, *tv;
        tp = strtok(tparams[j], "=");
        tv = strtok(NULL, "=");
        get_param_value(params, paramsnum, tp, tv, ret);
        if (strcmp(tp, "name") == 0) strcpy(pmodule->name,  ret);
        else if (tp[strlen(tp) - 1] == ':') {
            strcpy(pmodule->input_names[incount], ret);
            add_line_to_list(&ui.sidelist, "DEBUG: input[%d]: %s", incount, ret);
            incount += 1;
        }
        else if (strchr(tv, '.') != NULL) { // float
            if (strstr(ret, "hz:") != NULL) pmodule->fp[fcount] = 2 * sin(M_PI * (atof(strstr(ret, "hz:") + 3) / 132300.0)); // 44100 * 3
            else if (strstr(ret, "semi:") != NULL || strstr(ret, "cent:") != NULL) {
                int semi = 0, cent = 0;
                if (strstr(ret, "semi:") != NULL) semi = atoi(strstr(ret, "semi:") + 5);
                if (strstr(ret, "cent:") != NULL) cent = atoi(strstr(ret, "cent:") + 5);
                pmodule->fp[fcount] = calculate_freq_detune(semi, cent);
            }
            else pmodule->fp[fcount] = atof(ret);
            add_line_to_list(&ui.sidelist, "DEBUG: fp[%d]: %2.4f", fcount, pmodule->fp[fcount]);
            fcount += 1;
        }
        else if ((ret[1] != '/') && (ret[0] == '-' || (ret[0] >= '0' && ret[0] <= '9'))) { // int
            pmodule->ip[icount] = atoi(ret);
            add_line_to_list(&ui.sidelist, "DEBUG: ip[%d]: %d", icount, pmodule->ip[icount]);
            icount += 1;
        }
        else { // named int constant
            for (k = 0; k < 27; k++)
                if (strncmp(constants[k], ret, strlen(ret)) == 0) {
                    pmodule->ip[icount] = atoi(constants[k] + strlen(ret) + 1);
                    add_line_to_list(&ui.sidelist, "DEBUG: ip[%d]: %d from constant %s", icount, pmodule->ip[icount], ret);
                    icount += 1;
                    break;
                }
        }
    }
    if (pmodule->type == SEQ) {
        int i;
        for (i = 0; i < 16; i++) if (pmodule->fp[i + 1] == 123.456) pmodule->fp[i + 1] = pmodule->fp[0];
    }
    if (pmodule->type == ENV) {
        if (pmodule->fp[5] == 123.456) pmodule->fp[5] = pmodule->fp[9];
        if (pmodule->fp[7] == 123.456) pmodule->fp[7] = pmodule->fp[8];
        int i; for (i = 0; i < 5; i++) {
            if (pmodule->fp[i] == 0) pmodule->fp[i] = 0.000001;
            pmodule->fp[i] = time_delta / pmodule->fp[i];
        }
    }
    if (pmodule->type == DLY) {
        int n;
        int size = (int) (pmodule->fp[2] * 44100);
        pmodule->ip[0] = size;
        for (n = 0; n < VOICES; n++) {
            if (ppatch->voices[n].largedata[parsed_module] != NULL)
                free(ppatch->voices[n].largedata[parsed_module]);
            ppatch->voices[n].largedata[parsed_module] = (double *) malloc(sizeof(double) * size);
        }
    }
    if (pmodule->type == POL) {
        int i;
        pmodule->ip[0] = 0; // polynomial degree
        for (i = 0; i < 8; i++) if (pmodule->fp[i] != 0.0) pmodule->ip[0] = i;
    }
    if (pmodule->type == OSC) {
        int i;
        pmodule->ip[3] = -1;
        for (i = 0; i < ppatch->wavesused; i++)
            if (ppatch->waves[i].type == pmodule->ip[0] && ppatch->waves[i].harmonics == pmodule->ip[1] && ppatch->waves[i].pulsewidth == pmodule->ip[2])
                pmodule->ip[3] = i;
        if (pmodule->ip[3] != -1) return 0;
        int wn = ppatch->wavesused;
        pmodule->ip[3] = wn;
        ppatch->waves[wn].size = WAVEFORM_SAMPLES;
        ppatch->waves[wn].type = pmodule->ip[0];
        ppatch->waves[wn].harmonics = pmodule->ip[1];
        ppatch->waves[wn].pulsewidth = pmodule->ip[2];
        ppatch->waves[wn].data = make_wave(pmodule->ip[0], pmodule->ip[1], pmodule->ip[2], ppatch->waves[wn].size);
        ppatch->wavesused += 1;
    }
}

int read_patch_def_from_files(patch *ppatch, char **fileslist, int filestotal) {
    int parsed_module;
    char file_line[512];
    FILE *file;
    // stop all voices first
    int v, f, m, n, t;
    for (v = 0; v < VOICES; v++) patches[ui.selected_patch].voices[v].active = 0;
    
    // clean up modules
    for (m = 0; m < MODULES; m++) {
        ppatch->modules[m].type = NOP;
        strcpy(ppatch->modules[m].line, "");
        for (n = 0; n < VOICES; n++) // clean large data
            if (ppatch->voices[n].largedata[m] != NULL) {
                free(ppatch->voices[n].largedata[m]);
                ppatch->voices[n].largedata[m] = NULL;
            }
    }
    ppatch->wavesused = 0;
    clean_waveforms(ui.selected_patch);
    parsed_module = 0; // first module will be 1

    for (f = 0; f < filestotal; f++) {
        char filename_try[100] = "./bank/";
        strcat(filename_try, fileslist[f]);
        if ((file = fopen(filename_try, "r")) == NULL)
            if ((file = fopen(fileslist[f], "r")) == NULL) {
                add_line_to_list(&ui.sidelist, "File not found: %s", fileslist[f]);
                return 1;
            }
        
        // name read
        char *start;
        if (strrchr(fileslist[f], '/') == NULL) start = fileslist[f];
        else start = strrchr(fileslist[f], '/') + 1;
        if (f == 0) ppatch->name[0] = '\0'; else strcat(ppatch->name, " > ");
        strcat(ppatch->name, start);

        add_line_to_list(&ui.sidelist, "%s", "DEBUG: Reading patch");
        while (fgets(file_line, sizeof(file_line), file) != NULL) {
            file_line[strlen(file_line) - 1] = '\0';
            parsed_module += 1;
            parse_module_definition(ppatch, file_line, parsed_module);
        }
        ppatch->modulesused = parsed_module + 1;
        add_line_to_list(&ui.sidelist, "DEBUG: Modules used: %d", ppatch->modulesused);
        add_line_to_list(&ui.sidelist, "DEBUG: Waves used: %d", ppatch->wavesused);
        fclose(file);
    }
    
    // resolve module names, get module numbers
    for (m = 0; m < ppatch->modulesused; m++)
        for (n = 0; n < 8; n++) {
            char first = ppatch->modules[m].input_names[n][0];
            if (first == '#') ppatch->modules[m].inputs[n] = -1; // NULL - no input
            else if (first >= '0' && first <= '9') // absolute module number
                ppatch->modules[m].inputs[n] = atoi(ppatch->modules[m].input_names[n]);
            else if (first == '+' || first == '-') // relative module position
                ppatch->modules[m].inputs[n] = m + atoi(ppatch->modules[m].input_names[n]);
            else { // module name reference
                ppatch->modules[m].inputs[n] = 0; // will stay 0 if not found
                for (t = 0; t < ppatch->modulesused; t++)
                    if (strcmp(ppatch->modules[t].name, ppatch->modules[m].input_names[n]) == 0) {
                        ppatch->modules[m].inputs[n] = t;
                        break;
                    }
            }
        }
    // check for modules with constant output
    for (m = 0; m < ppatch->modulesused; m++) {
        ppatch->modules[m].constant = 0;
        if (ppatch->modules[m].type == MOD && ppatch->modules[m].inputs[0] == -1 && ppatch->modules[m].ip[0] != 3 && ppatch->modules[m].ip[0] != 5)
            ppatch->modules[m].constant = 1;
        if (ppatch->modules[m].type == SEQ && ppatch->modules[m].inputs[0] > -1)
            if (ppatch->modules[ppatch->modules[m].inputs[0]].constant == 1)
                ppatch->modules[m].constant = 1;
        if (ppatch->modules[m].constant == 1) add_line_to_list(&ui.sidelist, "Module %d has constant output", m);
    }
    show_patch(ui.selected_patch);
    return 0;
}

int execute_script(char *filename) {
    FILE *file;
    char file_line[512];
    char filename_try[100] = "./script/";
    strcat(filename_try, filename);
    if ((file = fopen(filename_try, "r")) == NULL)
        if ((file = fopen(filename, "r")) == NULL)
            return 1;
    while (fgets(file_line, sizeof(file_line), file) != NULL) {
            file_line[strlen(file_line) - 1] = '\0';
            add_line_to_list(&ui.sidelist, "%s", file_line);
            command_interpreter(file_line);
    }
    fclose(file);
    return 0;
}

int write_patch_def(patch *ppatch, char *filename) {
    FILE *file;
    if ((file = fopen(filename, "w")) == NULL) return 1;
    int i;
    for (i = 1; i < ppatch->modulesused; i++) {
        fputs(ppatch->modules[i].line, file);
        if (i < ppatch->modulesused - 1) fputs("\n", file);
    }
    fclose(file);
    return 0;
}

int execute_shell_command(char *line) {
    FILE *fp;
    char output[1000];
    char allout[10000];
    allout[0] = '\0';
    fp = popen(line, "r");
    if (fp == NULL) {
        add_line_to_list(&ui.sidelist, "Failed to run command: %s", line);
        return 0;
    }
    while (fgets(output, sizeof(output) - 1, fp) != NULL) {
        output[strlen(output) - 1] = '\0';
        strcat(allout, output);
        strcat(allout, " ");
    }
    add_long_line_to_list(&ui.sidelist, allout, ui.selected_patch, 0);
    pclose(fp);
}

int command_interpreter(char *line) {
    char *command;
    char *params[32];
    char ret[100];
    int paramsnum = 0;
    patch *ppatch = &patches[ui.selected_patch];
    char tmpline[LIST_LINE_LEN];
    strcpy(tmpline, line);
    command = strtok(tmpline, " \n");
    if (command == NULL) return -1;;
    while (1) {
        params[paramsnum] = strtok(NULL, " \n");
        if (params[paramsnum] == NULL) break;
        paramsnum++;
    }
    add_line_to_list(&ui.sidelist, "DEBUG: splitted [%s] [%d]", command, paramsnum);
    if (strncmp(line, "exec", 4) == 0) execute_shell_command(line + 5);
    else if (strcmp(command, "noteon") == 0) note_on(atoi(params[0]), atoi(params[1]), atoi(params[2]));
    else if (strcmp(command, "noteoff") == 0) note_off(atoi(params[0]), atoi(params[1]));
    else if (strcmp(command, "patch") == 0) ui.selected_patch = atoi(params[0]);
    else if (strcmp(command, "debug") == 0) ui.debug = atoi(params[0]);
    else if (strcmp(command, "messages") == 0) ui.showmessages = atoi(params[0]);
    else if (strcmp(command, "midichannel") == 0) patches[ui.selected_patch].midichannel = atoi(params[0]);
    else if (strcmp(command, "startnote") == 0) patches[ui.selected_patch].startnote = atoi(params[0]);
    else if (strcmp(command, "endnote") == 0) patches[ui.selected_patch].endnote = atoi(params[0]);
    else if (strcmp(command, "volume") == 0) patches[ui.selected_patch].volume = atof(params[0]);
    else if (strcmp(command, "minvelocity") == 0) patches[ui.selected_patch].minvelocity = atoi(params[0]);
    else if (strcmp(command, "showcolor") == 0) ui.showcolor = atoi(params[0]);
    else if (strcmp(command, "nextpatch") == 0 && ui.selected_patch < PATCHES - 1) ui.selected_patch++;
    else if (strcmp(command, "recstop") == 0) record_started = 0;
    else if (strcmp(command, "recwrite") == 0) write_stereo_wav_file(params[0], record_len);
    else if (strcmp(command, "recstart") == 0) {
        memset(record_buffer, 0, RECBUFFERSIZE * 2 * sizeof(short int));
        memset(record_len, 0, PATCHES * sizeof(int));
        record_started = 1;
    }
    else if (strcmp(command, "read") == 0) read_patch_def_from_files(ppatch, params, paramsnum);
    else if (strcmp(command, "script") == 0) execute_script(params[0]);
    else if (strcmp(command, "write") == 0) write_patch_def(ppatch, params[0]);
    show_patch(ui.selected_patch);
    return 0;
}

void initialize_default() {
    int i, j, k;
    time_delta = 1 / (double) Rate;
    for (i = 0; i < PATCHES; i++) {
        patches[i].midichannel = -1;
        patches[i].startnote = 0;
        patches[i].endnote = 127;
        patches[i].volume = 1.0;
        patches[i].minvelocity = 0;
        for (j = 0; j < MODULES; j++)
            for (k = 0; k < VOICES; k++)
                patches[i].voices[k].largedata[j] = NULL;
    }
    patches[0].midichannel = 0;
    ui.selected_patch = 0;
    double x, step;
    x = -16.0;
    step = 32.0 / (double) EXP2TABLESIZE;
    for (i = 0; i < EXP2TABLESIZE; i++) {
        exp2_table[i] = exp2(x);
        x += step;
    }
    exp2_table[0] = 0.0;
    x = 0.0;
    step = 1.01 / (double) SQRTTABLESIZE;
    for (i = 0; i < SQRTTABLESIZE; i++) {
        sqrt_table[i] = sqrt(x);
        x += step;
    }
    
    // init bank
    FILE *fp;
    ui.banksize = 0;
    fp = popen("find bank -type f | sort | sed s:bank/::g", "r");
    if (fp == NULL) {
        add_line_to_list(&ui.sidelist, "Failed to initialize patch bank");
        return;
    }
    while (fgets(ui.bank[ui.banksize], sizeof(ui.bank[ui.banksize]) - 1, fp) != NULL) {
        ui.bank[ui.banksize][strlen(ui.bank[ui.banksize]) - 1] = '\0';
        add_line_to_list(&ui.sidelist, "DEBUG: %s", ui.bank[ui.banksize]);
        ui.banksize += 1;
    }
    add_line_to_list(&ui.sidelist, "DEBUG: Finished reading bank");
    pclose(fp);
}

/***********************************************************************************************************************
 ALSA section
 Part of this ALSA handling code is based on examples found in this tutorial:
 http://alsamodular.sourceforge.net/alsa_programming_howto.html
***********************************************************************************************************************/

snd_seq_t *open_seq() {
    snd_seq_t *seq_handle;   
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "Cannot open ALSA sequencer.\n");
        return NULL;
    }
    snd_seq_set_client_name(seq_handle, "termsynth");
    if (snd_seq_create_simple_port(seq_handle, "termsynth", SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        fprintf(stderr, "Cannot create sequencer port.\n");
        return NULL;
    }
    return seq_handle;
}

snd_pcm_t *open_pcm(char *pcm_name) {
    snd_pcm_t *playback_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;
    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf (stderr, "Cannot open audio device %s\n", pcm_name);
        exit(1);
    }
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &Rate, 0);
    add_line_to_list(&ui.sidelist, "INFO: Rate is %d", Rate);
    snd_pcm_hw_params_set_channels(playback_handle, hw_params, 2);
    snd_pcm_hw_params_set_periods(playback_handle, hw_params, 4, 0); // Setting periods to 4 significantly reduced buffer problems
    snd_pcm_hw_params_set_period_size(playback_handle, hw_params, BUFFERSIZE, 0);
    snd_pcm_hw_params(playback_handle, hw_params);
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(playback_handle, sw_params);
    snd_pcm_sw_params_set_avail_min(playback_handle, sw_params, BUFFERSIZE);
    snd_pcm_sw_params(playback_handle, sw_params);
    return playback_handle;
}

void note_on(int note, int channel, int velocity) {
    int i, j, k;
    voice *pvoice;
    for (i = 0; i < PATCHES; i++) {
        if (patches[i].midichannel == channel && note >= patches[i].startnote && note <= patches[i].endnote) {
            for (j = 0; j < VOICES; j++) {
                pvoice = &patches[i].voices[j];
                if (!pvoice->active) {
                    pvoice->started = 0;
                    pvoice->gate = 1;
                    pvoice->notenumber = note;
                    pvoice->frequency = 440 * pow(2, ((double)note - 69) / 12.0);
                    pvoice->scaledfrequency = pvoice->frequency / 22050.0;
                    pvoice->invertedfrequency = 1.0 / pvoice->frequency;
                    pvoice->onchannel = channel;
                    int vel = velocity;
                    if (vel < patches[i].minvelocity) vel = patches[i].minvelocity;
                    pvoice->velocity = vel / 127.0;
                    pvoice->active = 1;
                    pvoice->zerocount = 0;
                    memset(pvoice->moduledata, 0, MODULES * 8 * sizeof(double));
                    memset(pvoice->imoduledata, 0, MODULES * 4 * sizeof(int));
                    memset(pvoice->out, 0, MODULES * sizeof(double));
                    for (k = 0; k < MODULES; k++)
                        if (pvoice->largedata[k] != NULL) // this will work only for DLY
                            memset(pvoice->largedata[k], 0, patches[i].modules[k].ip[0] * sizeof(double));
                    if (ui.showmessages == 0) break;
                    add_line_to_list(&ui.sidelist, TERM_256_FCOLOR "[Patch %d]" TERM_ALL_OFF " %d ON %2.2f Hz", i, i, note, pvoice->frequency);
                    break;
                }
            }
        }
    }
}

void note_off(int note, int channel) {
    int i, j, k;
    voice *pvoice;
    for (i = 0; i < PATCHES; i++) {
        for (j = 0; j < VOICES; j++) {
            pvoice = &patches[i].voices[j];
            if (pvoice->gate && pvoice->active && (pvoice->notenumber == note) && (pvoice->onchannel == channel)) {
                if (ui.showmessages == 1)
                    add_line_to_list(&ui.sidelist, TERM_256_FCOLOR "[Patch %d]" TERM_ALL_OFF " %d OFF", i, i, note);
                pvoice->gate = 0;
                for (k = 0; k < MODULES; k++) {
                    // reset all ENV time to zero - used for release phase
                    if (patches[i].modules[k].type == ENV)
                        pvoice->moduledata[k][1] = 0.0;
                }
            }
        }
    }
}

int midi_callback() {
    snd_seq_event_t *ev;
    int i, k;
    do {
        snd_seq_event_input(seq_handle, &ev);
        if (ev->type == SND_SEQ_EVENT_PITCHBEND || ev->type == SND_SEQ_EVENT_CONTROLLER) {
            add_line_to_list(&ui.sidelist, "DEBUG: [Controller %d]" TERM_ALL_OFF " value %d", ev->data.control.param, ev->data.control.value);
            for (i = 0; i < PATCHES; i++)
                if (patches[i].midichannel == ev->data.control.channel)
                    for (k = 0; k < MODULES; k++)
                        if (patches[i].modules[k].type == MOD) {
                            if (ev->type == SND_SEQ_EVENT_CONTROLLER && patches[i].modules[k].ip[0] == 3
                              && patches[i].modules[k].ip[1] == ev->data.control.param) // controller
                                patches[i].modules[k].midicc = (double)ev->data.control.value * (1.0 / 127.0);
                            if (ev->type == SND_SEQ_EVENT_PITCHBEND && patches[i].modules[k].ip[0] == 5) // pitch wheel
                                patches[i].modules[k].pitchwheel = (double)ev->data.control.value * (1.0 / 8192.0);
                        }
        }
        if (ev->type == SND_SEQ_EVENT_NOTEON)
            note_on(ev->data.note.note, ev->data.control.channel, ev->data.note.velocity);
        if (ev->type == SND_SEQ_EVENT_NOTEOFF)
            note_off(ev->data.note.note, ev->data.control.channel);
        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
    return 0;
}

/***********************************************************************************************************************
 module processing section 
***********************************************************************************************************************/

static inline double exp2_fast(double x) {
    static double inv_logical_size = 0.03125; // 1.0/32.0
    double pos = EXP2TABLESIZE * (x + 16) * inv_logical_size;
    int pos1 = (int) pos;
    int pos2 = pos1 + 1;
    double r = pos - pos1;
    if (pos1 < 0 || pos2 >= EXP2TABLESIZE) {
        add_line_to_list(&ui.sidelist, "DEBUG: exp2_fast table overflow for x = %2.4f", x);
        return exp2(x);
    }
    return exp2_table[pos1] + r * (exp2_table[pos2] - exp2_table[pos1]);
}

static inline double sqrt_fast(double x) {
    static double inv_logical_size = 0.9900990099; // 1.0/1.01
    double pos = SQRTTABLESIZE * (x + 0) * inv_logical_size;
    return sqrt_table[(int) pos];
}

static inline void nop(module *pmodule, voice *pvoice, int m) {
    pvoice->out[m] = 0;
}

static inline void pol(module *pmodule, voice *pvoice, int m) {
    int a;
    double x = 1.0;
    pvoice->out[m] = 0;
    for (a = 0; a <= pmodule->ip[0]; a++) {
        pvoice->out[m] += pmodule->fp[a] * x;
        x *= pvoice->out[pmodule->inputs[0]];
    }
}

static inline void dly(module *pmodule, voice *pvoice, int m) {
    double input = pvoice->out[pmodule->inputs[0]];
    double readpos = pvoice->moduledata[m][1]; // + 1;
    double amount = pmodule->fp[0];
    double feedback = pmodule->fp[1];
    if (pmodule->inputs[1] > -1) readpos += pvoice->out[pmodule->inputs[1]] * 44100;
    readpos = fmod(readpos, pmodule->ip[0]);
    int x1 = (int) readpos;
    int x2 = x1 + 1;
    x2 %= pmodule->ip[0];
    double estvalue = (pvoice->largedata[m][x2] - pvoice->largedata[m][x1]) * (readpos - floor(readpos)) + pvoice->largedata[m][x1];
    double y = input * (1.0 - amount) + estvalue * amount;    
    pvoice->largedata[m][(int) pvoice->moduledata[m][1]] = input * (1 - feedback) + estvalue * feedback;    
    pvoice->moduledata[m][1] += 1;
    pvoice->moduledata[m][1] = (int) pvoice->moduledata[m][1] % pmodule->ip[0];
    pvoice->out[m] = y;
}

static inline void env(module *pmodule, voice *pvoice, int m) {
    int shape = pmodule->ip[0];
    double t = pvoice->moduledata[m][1];
    double *tparam = &(pmodule->fp[0]);
    double *lparam = &(pmodule->fp[5]);
    double *lastenvlevel = &(pvoice->moduledata[m][2]);
    int *phase = &(pvoice->imoduledata[m][0]);
    if (shape == 0) t = sqrt_fast(t); // for naturally curved envelope
    if (!pvoice->gate && *phase < 5) *phase = 5;
    if (*phase == 4)  pvoice->out[m] = *lastenvlevel = lparam[3]; // sustain
        else if (*phase == 0)  pvoice->out[m] = *lastenvlevel = lparam[0]; // delay
        else if (*phase == 5) pvoice->out[m] = t * (lparam[4] - *lastenvlevel) + *lastenvlevel; // release
        else if (*phase == 6) pvoice->out[m] = lparam[4]; // end
        else pvoice->out[m] = *lastenvlevel = t * (lparam[*phase] - lparam[*phase - 1]) + lparam[*phase - 1];
    double *time = &(pvoice->moduledata[m][1]);
    if (*phase < 4) *time += tparam[*phase];
        else if (*phase == 5) *time += tparam[4];
    if (*time > 1) {
        *time = 0;
        *phase += 1;
    }
    if (pmodule->inputs[0] > -1) pvoice->out[m] *= pvoice->out[pmodule->inputs[0]]; // amp modulation of input
}

static inline double get_wave_value(double phase, int patchnr, int wave_nr) {
    static double inv_pi = 1 / M_PI;
    int size = patches[patchnr].waves[wave_nr].size;
    double pos = phase * inv_pi * 0.5 * size;
    int pos1 = (int) pos;
    int pos2 = pos1 + 1;
    double r = pos - pos1;
    if (pos2 == size) pos2 = 0;
    return patches[patchnr].waves[wave_nr].data[pos1] * (1.0 - r)
        + patches[patchnr].waves[wave_nr].data[pos2] * r;
}

static inline double twopirange(double angle) {
    static double twopi = 2 * M_PI;
    while (angle > twopi) angle -= twopi;
    while (angle < 0) angle += twopi;
    return angle;
}

static inline void osc(module *pmodule, voice *pvoice, int m) {
    double fF, phaseDelta;
    static double two_pi = 2 * M_PI;
    fF = pvoice->frequency;
    fF *= pmodule->fp[0]; // * freq detune
    fF += pmodule->fp[1]; // + fixed frequency offset
    if (pmodule->inputs[1] > -1) fF *= exp2_fast(pvoice->out[pmodule->inputs[1]]); // pitch modulation
    phaseDelta = two_pi * fF * time_delta;
    pvoice->moduledata[m][1] += phaseDelta;
    pvoice->moduledata[m][1] = twopirange(pvoice->moduledata[m][1]);
    if (pmodule->inputs[0] > -1) { // phase modulation (FM)
        double modPhase = twopirange(pvoice->moduledata[m][1] + pvoice->out[pmodule->inputs[0]]);
        pvoice->out[m] = get_wave_value(modPhase, pmodule->patchnr, pmodule->ip[3]);
    } else pvoice->out[m] = get_wave_value(pvoice->moduledata[m][1], pmodule->patchnr, pmodule->ip[3]); // (phase, patch, table)
    if (pmodule->inputs[2] > -1) pvoice->out[m] *= pvoice->out[pmodule->inputs[2]]; // amp modulation
}

static inline void seq(module *pmodule, voice *pvoice, int m) {
    int step = (int) pvoice->out[pmodule->inputs[0]];
    double r = pvoice->out[pmodule->inputs[0]] - step;
    int nextstep = step + 1;
    if (pmodule->ip[1] == 0) { // if not wrap
       if (step >= pmodule->ip[0]) step = pmodule->ip[0] - 1;
       if (nextstep >= pmodule->ip[0]) nextstep = pmodule->ip[0] - 1;
    } else {
       step = step % pmodule->ip[0];
       nextstep = nextstep % pmodule->ip[0];
    }
    pvoice->out[m] = pmodule->fp[step + 1] * (1.0 - r) + pmodule->fp[nextstep + 1] * r;
}

static inline void clk(module *pmodule, voice *pvoice, int m) {
    pvoice->moduledata[m][0] += time_delta;
    double clocktime = pmodule->fp[0];
    if (pmodule->inputs[0] > -1) clocktime += pvoice->out[pmodule->inputs[0]];
    if (pvoice->moduledata[m][0] > clocktime) {
        pvoice->out[m] = 1.0;
        pvoice->moduledata[m][0] = 0.0;
    }
    else pvoice->out[m] = 0.0;
}

static inline double state_variable_filter(int filterType, double *buffer, double inputValue, double resonance, double cutoff) {
    // 0-Low 1-High 2-BandPass 3-BandStop
    int i;
    double inputs[3]; // 3x oversampled filter, sample rate = 44100 x 3, [0, 1] = [0 Hz, 22050 Hz]
    inputs[0] = inputValue * 0.333333 + buffer[4] * 0.666667;
    inputs[1] = inputValue * 0.666667 + buffer[4] * 0.333333;
    inputs[2] = inputValue;
    for (i = 0; i < 3; i++) {
        buffer[2] = buffer[2] - 0.001 * buffer[2] * buffer[2] * buffer[2]; // wave-shape soft clip
        buffer[0] = buffer[0] + cutoff * buffer[2];
        buffer[1] = inputs[i] - buffer[0] - (1 - resonance) * buffer[2];
        buffer[2] = buffer[2] + cutoff * buffer[1];
        buffer[3] = buffer[0] + buffer[1];
    }
    buffer[4] = inputValue;
    return buffer[filterType];
}

static inline void flt(module *pmodule, voice *pvoice, int m) {
    double cutoff = pmodule->fp[0];
    double res = pmodule->fp[1];
    if (pmodule->inputs[1] > -1)
        cutoff += pvoice->out[pmodule->inputs[1]];
    if (pmodule->inputs[2] > -1)
        res += pvoice->out[pmodule->inputs[2]];
    if (cutoff > 1.0) cutoff = 1.0;
    if (cutoff < 0.0) cutoff = 0.0;
    if (res > 1.0) res = 1.0;
    if (res < 0.0) res = 0.0;
    pvoice->out[m] = state_variable_filter(pmodule->ip[0], 
        &(pvoice->moduledata[m][1]), pvoice->out[pmodule->inputs[0]], res, cutoff);
}

static inline void mix(module *pmodule, voice *pvoice, int m) {
    if (pmodule->inputs[0] < 0) { // no cross-fade
        pvoice->out[m] = pvoice->out[pmodule->inputs[1]] + pvoice->out[pmodule->inputs[2]]
                                   + pvoice->out[pmodule->inputs[3]] + pvoice->out[pmodule->inputs[4]]
                                   + pvoice->out[pmodule->inputs[5]] + pvoice->out[pmodule->inputs[6]];
    } else {
        double amount = pvoice->out[pmodule->inputs[0]];
        int pos = (int) amount;
        amount -= pos;
        pvoice->out[m] = (1 - amount) * pvoice->out[pmodule->inputs[pos + 1]] +
            amount * pvoice->out[pmodule->inputs[pos + 2]];
    }
}

static inline void mod(module *pmodule, voice *pvoice, int m) {
    if (pmodule->inputs[0] > -1) pvoice->out[m] = pvoice->out[pmodule->inputs[0]];
    else {
        if (pmodule->ip[0] == 3) pvoice->out[m] = pmodule->midicc; // read already calculated values
        if (pmodule->ip[0] == 2) pvoice->out[m] = pvoice->velocity;
        if (pmodule->ip[0] == 1) pvoice->out[m] = pvoice->scaledfrequency;
        if (pmodule->ip[0] == 4) pvoice->out[m] = pvoice->invertedfrequency;
        if (pmodule->ip[0] == 5) pvoice->out[m] = pmodule->pitchwheel;
        if (pmodule->ip[0] == 6) pvoice->out[m] = pvoice->notenumber;
    }
    pvoice->out[m] *= pmodule->fp[0]; // * amp
    pvoice->out[m] += pmodule->fp[1]; // + offset
    if (pmodule->ip[2] == 1) pvoice->out[m] = exp2_fast(pvoice->out[m]);
    else if (pmodule->ip[2] == 3) pvoice->out[m] = (int) pvoice->out[m];
    else if (pmodule->ip[2] == 4) pvoice->out[m] = fabs(pvoice->out[m]);
    else if (pmodule->ip[2] == 2) {
        if (pvoice->out[m] == 0.0) pvoice->out[m] = 0.000001;
        pvoice->out[m] = 1.0 / pvoice->out[m];
    }
}

static inline void mul(module *pmodule, voice *pvoice, int m) {
    pvoice->out[m] = pvoice->out[pmodule->inputs[0]] * pvoice->out[pmodule->inputs[1]];
}

static inline void out(module *pmodule, voice *pvoice, int m) {
    pvoice->moduledata[m][0] = pvoice->out[pmodule->inputs[0]];
    pvoice->moduledata[m][1] = pvoice->out[pmodule->inputs[1]];
    pvoice->out[m] = pvoice->moduledata[m][0] + pvoice->moduledata[m][1];
    pvoice->out[m] *= 0.5;
}

int playback_callback(snd_pcm_sframes_t nframes) {
    int i, j, k, m;
    double voices_left, voices_right;
    voice *pvoice;
    module *pmodule;
    static void (*mod_fun[]) (module *pmodule, voice *pvoice, int m) =
        {nop, osc, env, mix, mul, flt, dly, seq, pol, mod, clk, out};
    memset(buf, 0, nframes * 4); // clean buffer
    for (i = 0; i < PATCHES; i++) {
        int lastmodule = patches[i].modulesused - 1;
        if (patches[i].midichannel == -1) continue; // if not used
        for (j = 0; j < nframes; j++) {
            voices_left = voices_right = 0.0;
            for (k = 0; k < VOICES; k++) {
                pvoice = &patches[i].voices[k];
                if (pvoice->active == 0) continue;
                for (m = 1; m <= lastmodule; m++) {
                    pmodule = &patches[i].modules[m];
                    if (pmodule->constant == 0 || (pmodule->constant == 1 && pvoice->started == 0))
#ifdef INLINEMODULES
                    switch (pmodule->type) {
                        case NOP: nop(pmodule, pvoice, m); break;
                        case OSC: osc(pmodule, pvoice, m); break;
                        case ENV: env(pmodule, pvoice, m); break;
                        case MIX: mix(pmodule, pvoice, m); break;
                        case MUL: mul(pmodule, pvoice, m); break;
                        case FLT: flt(pmodule, pvoice, m); break;
                        case DLY: dly(pmodule, pvoice, m); break;
                        case SEQ: seq(pmodule, pvoice, m); break;
                        case POL: pol(pmodule, pvoice, m); break;
                        case MOD: mod(pmodule, pvoice, m); break;
                        case CLK: clk(pmodule, pvoice, m); break;
                        case OUT: out(pmodule, pvoice, m); break;
                    }
#else
                        (*mod_fun[pmodule->type]) (pmodule, pvoice, m);
#endif
                } // modules loop end
                pvoice->started = 1;
                if (pvoice->gate == 0) {
                    if ((int) (pvoice->out[lastmodule] * GAIN) == 0) pvoice->zerocount++;
                    else pvoice->zerocount = 0;
                    if (pvoice->zerocount > nframes) {
                        pvoice->active = 0;
                        if (ui.showmessages == 1) {
                            if (ui.showcolor) add_line_to_list(&ui.sidelist, TERM_256_FCOLOR "[Patch %d]" TERM_ALL_OFF " Note %d inactive", i, i, pvoice->notenumber);
                            else add_line_to_list(&ui.sidelist, "[Patch %d]" TERM_ALL_OFF " Note %d inactive", i, pvoice->notenumber);
                        }
                    }
                }
                if (patches[i].modules[lastmodule].type == OUT) {
                    voices_left += pvoice->moduledata[lastmodule][0];
                    voices_right += pvoice->moduledata[lastmodule][1];
                } else {
                    voices_left += pvoice->out[lastmodule];
                    voices_right = voices_left;
                }
            } // voices loop end
            voices_left = voices_left * patches[i].volume * GAIN;
            voices_right = voices_right * patches[i].volume * GAIN;
            buf[2 * j] += (int) (voices_left);
            buf[2 * j + 1] += (int) (voices_right);
            if (record_started == 1) {
                record_buffer[2 * record_len[i]] += voices_left;
                record_buffer[2 * record_len[i] + 1] += voices_right;
                record_len[i] += 1;
                if (record_len[i] == RECBUFFERSIZE) {
                    record_started = 0;
                    add_line_to_list(&ui.sidelist, TERM_INVERTED "Recording stopped - reached buffer end");
                }
            }
        } // frames loop end
    } // patches loop end
    return snd_pcm_writei (playback_handle, buf, nframes);
}

/***********************************************************************************************************************
 main 
***********************************************************************************************************************/

int main(int argc, char *argv[]) {
    pthread_t pth_io;
    int nfds, seq_nfds, i, j;
    struct pollfd *pfds;
    if (argc < 2) {
        printf("termsynth <device>\n"); 
        return -1;
    }
    signal(SIGWINCH, handle_winch);
    printf(TERM_HIDE_CURSOR TERM_ALTER_SCREEN);
    handle_winch(SIGWINCH); // read initial terminal size
    buf = (short *) malloc (2 * sizeof(short) * BUFFERSIZE);
    playback_handle = open_pcm(argv[1]);
    if ((seq_handle = open_seq()) == NULL) return -1;
    seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    nfds = snd_pcm_poll_descriptors_count(playback_handle);
    pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (seq_nfds + nfds));
    snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
    snd_pcm_poll_descriptors(playback_handle, pfds + seq_nfds, nfds); 
    initialize_default();
    for (i = 0; i < PATCHES; i++)
        for (j = 0; j < VOICES; j++)
            patches[i].voices[j].active = 0;
    pthread_create(&pth_io, NULL, io_thread_function, "");
    srand(time(NULL));
    char initcommand[20] = "script init";
    command_interpreter(initcommand);
    while (!ui.quit_program) {
        usleep(1);
        if (poll(pfds, seq_nfds + nfds, 1000) > 0) {
            for (i = 0; i < seq_nfds; i++) {
                if (pfds[i].revents > 0) {
                    midi_callback();
                }
            }
            for (i = seq_nfds; i < seq_nfds + nfds; i++)
                if (pfds[i].revents > 0)
                    if (playback_callback(BUFFERSIZE) < BUFFERSIZE) {
                        buffer_underrun_count += 1;
                        add_line_to_list(&ui.sidelist, TERM_INVERTED "Buffer underrun (%d)", buffer_underrun_count);
                        snd_pcm_prepare(playback_handle);
                    }
        }
    }
    add_line_to_list(&ui.sidelist, "DEBUG: Ending IO thread");
    pthread_join(pth_io, NULL);
    snd_pcm_close(playback_handle);
    snd_seq_close(seq_handle);
    free(buf);
    clean_all();
    printf(TERM_ALL_OFF TERM_CLEAR TERM_NORMAL_SCREEN TERM_SHOW_CURSOR);
    return 0;
}
