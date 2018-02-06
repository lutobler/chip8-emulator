/**
 * Copyright (c) 2018, Lukas Tobler
 * GNU General Public License v3.0
 * (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
 *
 * An emulator (or interpreter) for the Chip8 computer and programming
 * language.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <time.h>
#include <getopt.h>

/* SDL default window size */
#define SDL_WIN_W 640
#define SDL_WIN_H 320

/* Chip8 hardware parameters */
#define MEM_SIZE 4096
#define RESERVED_MEM 512
#define DISP_W 64
#define DISP_H 32
#define DISP_SIZE DISP_W*DISP_H
#define STACK_SIZE 16
#define _60HZ 16666667L /* (1/60) seconds in ns */
#define VF V[15]

/* Chip8 font. Digits 0-F, 5 bytes each. */
static const uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70,
    0xF0, 0x10, 0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0,
    0x90, 0x90, 0xF0, 0x10, 0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0,
    0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0, 0x10, 0x20, 0x40, 0x40,
    0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0, 0x10, 0xF0,
    0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
    0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0,
    0xF0, 0x80, 0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80
};

enum chip8_key {
    C8K_0 = 0x0, C8K_1 = 0x1, C8K_2 = 0x2, C8K_3 = 0x3,
    C8K_4 = 0x4, C8K_5 = 0x5, C8K_6 = 0x6, C8K_7 = 0x7,
    C8K_8 = 0x8, C8K_9 = 0x9, C8K_A = 0xA, C8K_B = 0xB,
    C8K_C = 0xC, C8K_D = 0xD, C8K_E = 0xE, C8K_F = 0xF,
    C8K_NONE = -1
};

enum eml_stat {
    EML_OK,                                 /* No errors during cycle */
    EML_REDRAW,                             /* Display redraw required */
    EML_WAIT_KEY,                           /* Waiting for key press */
    EML_BRK_REACHED,                        /* Breakpoint reached */
    EML_UNK_OPC,                            /* Error: Unknown opcode */
    EML_STACK_OVERFL,                       /* Error: Stack overflow */
    EML_STACK_UNDERFL,                      /* Error: Stack is empty */
    EML_PC_OVERFL                           /* Error: PC outside memory */
};

/* Chip8 machine state */
static uint8_t memory[MEM_SIZE];            /* 4096 bytes of memory */
static uint8_t display[DISP_SIZE];          /* 64x32 pixel monochrome display */
static uint8_t V[16];                       /* V0 to VF data registers */
static uint8_t SP;                          /* Stack pointer (next free slot) */
static uint8_t DT;                          /* Delay timer */
static uint8_t ST;                          /* Sound timer */
static uint16_t PC = 0x200;                 /* Program counter. Start at 0x200 */
static uint16_t I;                          /* Address register */
static uint16_t stack[STACK_SIZE];          /* Stack */

/* Emulator data */
static uint16_t opcode;                     /* Last read opcode */
static uint16_t prev_PC;                    /* Previous PC (for error output) */
static uint16_t keypad;                     /* Bitmap for pressed keys, 0-F */
static enum chip8_key last_key = C8K_NONE;  /* The last pressed key */
static bool key_waiting = false;            /* Emulator is wating for a key */
static bool dbg_output = false;             /* Debug output flag */
static bool brk_point_set = false;          /* Breakpoint enable flag */
static uint32_t brk_point;                  /* Curent breakpoint */
static int32_t clock_speed = 1080;          /* Clock speed in Hz */

/* SDL */
static SDL_Window *window;
static SDL_Renderer *renderer;

/* Map SDL_Keycode -> Chip8 Keypad */
static uint8_t sdlk_to_c8k(SDL_Keycode key) {
    switch (key) {
    case SDLK_1: return C8K_1; break; case SDLK_2: return C8K_2; break;
    case SDLK_3: return C8K_3; break; case SDLK_4: return C8K_C; break;
    case SDLK_q: return C8K_4; break; case SDLK_w: return C8K_5; break;
    case SDLK_e: return C8K_6; break; case SDLK_r: return C8K_D; break;
    case SDLK_a: return C8K_7; break; case SDLK_s: return C8K_8; break;
    case SDLK_d: return C8K_9; break; case SDLK_f: return C8K_E; break;
    case SDLK_y: return C8K_A; break; case SDLK_x: return C8K_0; break;
    case SDLK_c: return C8K_B; break; case SDLK_v: return C8K_F; break;
    default: return C8K_0;
    }
}

/* Set a key on the keypad as pressed */
static inline void keypad_pressed(SDL_Keycode key) {
    keypad |= 1 << (sdlk_to_c8k(key));
    if (key_waiting) {
        last_key = sdlk_to_c8k(key);
        key_waiting = false;
    }
}

/* Set a key on the keypad as released */
static inline void keypad_released(SDL_Keycode key) {
    keypad ^= 1 << (sdlk_to_c8k(key));
}

/* Test if a key is pressed */
static inline bool keypad_is_pressed(enum chip8_key key) {
    return keypad & (1 << key);
}

/* Extract the x nibble from an opcode */
static inline uint8_t op_x(uint16_t opcode) { return (opcode >> 8) & 0xF; }

/* Extract the y nibble from an opcode */
static inline uint8_t op_y(uint16_t opcode) { return (opcode >> 4) & 0xF; }

/* Extract the kk byte from an opcode */
static inline uint8_t op_kk(uint16_t opcode) { return opcode & 0xFF; }

/* Extract the nnn 12-bit number from an opcode */
static inline uint16_t op_nnn(uint16_t opcode) { return opcode & 0xFFF; }

enum eml_stat _b0() {
    switch (opcode & 0x00FF) {
    case 0x00E0: /* 00E0 - CLS: Clear the display. */
        memset(&display, 0, DISP_SIZE);
        return EML_REDRAW;
    case 0x00EE: /* 00EE - RET: Return from a subroutine. */
        if (SP == 0)
            return EML_STACK_UNDERFL;
        PC = stack[--SP];
        return EML_OK;
    }
    return EML_UNK_OPC;
}

/* 1nnn - JP addr: Jump to location nnn. */
enum eml_stat _b1() {
    PC = op_nnn(opcode);
    return EML_OK;
}

/* 2nnn - CALL addr: Call subroutine at nnn. */
enum eml_stat _b2() {
    if (SP == STACK_SIZE)
        return EML_STACK_OVERFL;
    stack[SP++] = PC;
    PC = op_nnn(opcode);
    return EML_OK;
}

/* 3xkk - SE Vx, byte: Skip next instruction if Vx = kk. */
enum eml_stat _b3() {
    if (V[op_x(opcode)] == op_kk(opcode))
        PC += 2;
    return EML_OK;
}

/* 4xkk - SNE Vx, byte: Skip next instruction if Vx != kk. */
enum eml_stat _b4() {
    if (V[op_x(opcode)] != op_kk(opcode))
        PC += 2;
    return EML_OK;
}

/* 5xy0 - SE Vx, Vy: Skip next instruction if Vx = Vy. */
enum eml_stat _b5() {
    if (V[op_x(opcode)] == V[op_y(opcode)])
        PC += 2;
    return EML_OK;
}

/* 6xkk - LD Vx, byte: Set Vx = kk. */
enum eml_stat _b6() {
    V[op_x(opcode)] = op_kk(opcode);
    return EML_OK;
}

/* 7xkk - ADD Vx, byte: Set Vx = Vx + kk. */
enum eml_stat _b7() {
    V[op_x(opcode)] += op_kk(opcode);
    return EML_OK;
}

enum eml_stat _b8() {
    switch (opcode & 0x000F) {
    /* 8xy0 - LD Vx, Vy: Set Vx = Vy. */
    case 0x0000:
        V[op_x(opcode)] = V[op_y(opcode)];
        break;
    /* 8xy1 - OR Vx, Vy: Set Vx = Vx OR Vy. */
    case 0x0001:
        V[op_x(opcode)] |= V[op_y(opcode)];
        break;
    /* 8xy2 - AND Vx, Vy: Set Vx = Vx AND Vy. */
    case 0x0002:
        V[op_x(opcode)] &= V[op_y(opcode)];
        break;
    /* 8xy3 - XOR Vx, Vy: Set Vx = Vx XOR Vy. */
    case 0x0003:
        V[op_x(opcode)] ^= V[op_y(opcode)];
        break;
    /* 8xy4 - ADD Vx, Vy: Set Vx = Vx + Vy, set VF = carry. */
    case 0x0004: {
        uint16_t s = (uint16_t) V[op_x(opcode)] + (uint16_t) V[op_y(opcode)];
        VF = (s > 0xFF) ? 1 : 0;
        V[op_x(opcode)] = s & 0xFF;
        break;
    }
    /* 8xy5 - SUB Vx, Vy: Set Vx = Vx - Vy, set VF = NOT borrow. */
    case 0x0005:
        VF = (V[op_x(opcode)] > V[op_y(opcode)]) ? 1 : 0;
        V[op_x(opcode)] -= V[op_y(opcode)];
        break;
    /* 8xy6 - SHR Vx {, Vy}: Set Vx = Vx SHR 1. */
    case 0x0006:
        VF = V[op_x(opcode)] & 0x01;
        V[op_x(opcode)] = V[op_x(opcode)] >> 1;
        break;
    /* 8xy7 - SUBN Vx, Vy: Set Vx = Vy - Vx, set VF = NOT borrow. */
    case 0x0007:
        VF = (V[op_x(opcode)] < V[op_y(opcode)]) ? 1 : 0;
        V[op_x(opcode)] = V[op_y(opcode)] - V[op_x(opcode)];
        break;
    /* 8xyE - SHL Vx {, Vy}: Set Vx = Vx SHL 1. */
    case 0x000E:
        VF = (V[op_x(opcode)] & 0x80) >> 7;
        V[op_x(opcode)] = V[op_x(opcode)] << 1;
        break;
    default:
        return EML_UNK_OPC;
    }
    return EML_OK;
}

/* 9xy0 - SNE Vx, Vy: Skip next instruction if Vx != Vy. */
enum eml_stat _b9() {
    if (V[op_x(opcode)] != V[op_y(opcode)])
        PC += 2;
    return EML_OK;
}

/* Annn - LD I, addr: Set I = nnn. */
enum eml_stat _bA() {
    I = op_nnn(opcode);
    return EML_OK;
}

/* Bnnn - JP V0, addr: Jump to location nnn + V0. */
enum eml_stat _bB() {
    PC = op_nnn(opcode) + V[0];
    return EML_OK;
}

/* Cxkk - RND Vx, byte: Set Vx = random byte AND kk. */
enum eml_stat _bC() {
    V[op_x(opcode)] = rand() & op_kk(opcode);
    return EML_OK;
}

/*
 * Dxyn - DRW Vx, Vy, nibble:
 * Display n-byte sprite starting at memory location I at (Vx, Vy),
 * set VF = collision.
 */
enum eml_stat _bD() {
    uint8_t n = opcode & 0xF;
    uint8_t x = V[op_x(opcode)];
    uint8_t y = V[op_y(opcode)];
    uint8_t collide = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 8; j++) {
            uint32_t d_idx = ((i + y) % DISP_H)*DISP_W + (x + j) % DISP_W;
            uint8_t sprt_bit = (memory[I + i] >> (7 - j)) & 0x1;
            if (display[d_idx] == 1 && sprt_bit == 1)
                collide = 1;
            display[d_idx] ^= sprt_bit;
        }
    }

    VF = collide;
    return EML_REDRAW;
}

enum eml_stat _bE() {
    switch (opcode & 0x00FF) {
    /*
     * Ex9E - SKP Vx:
     * Skip next instruction if key with the value of Vx is pressed.
     */
    case 0x009E:
        if (keypad_is_pressed(V[op_x(opcode)]))
            PC += 2;
        return EML_OK;
    /*
     * ExA1 - SKNP Vx:
     * Skip next instruction if key with the value of Vx is not pressed.
     */
    case 0x00A1:
        if (!keypad_is_pressed(V[op_x(opcode)]))
            PC += 2;
        return EML_OK;
    default:
        return EML_UNK_OPC;
    }
}

enum eml_stat _bF() {
    switch (opcode & 0x00FF) {
    /* Fx07 - LD Vx, DT: Set Vx = delay timer value. */
    case 0x0007:
        V[op_x(opcode)] = DT;
        return EML_OK;
    /* Fx0A - LD Vx, K: Wait for a key press, store the value of the key in Vx. */
    case 0x000A:
        if (last_key == C8K_NONE) { /* start waiting-for-key process */
            key_waiting = true;
            return EML_WAIT_KEY;
        } else { /* stop waiting */
            V[op_x(opcode)] = last_key;
            last_key = C8K_NONE;
            return EML_OK;
        }
    /* Fx15 - LD DT, Vx: Set delay timer = Vx. */
    case 0x0015:
        DT = V[op_x(opcode)];
        return EML_OK;
    /* Fx18 - LD ST, Vx: Set sound timer = Vx. */
    case 0x0018:
        ST = V[op_x(opcode)];
        return EML_OK;
    /* Fx1E - ADD I, Vx: Set I = I + Vx. */
    case 0x001E:
        I += V[op_x(opcode)];
        VF = (I > 0xFFF) ? 1 : 0; /* undocumented feature */
        return EML_OK;
    /* Fx29 - LD F, Vx: Set I = location of sprite for digit Vx. */
    case 0x0029:
        I = V[op_x(opcode)] * 5;
        return EML_OK;
    /*
     * Fx33 - LD B, Vx:
     * Store BCD representation of Vx in memory locations I, I+1, and I+2.
     */
    case 0x0033: {
        uint8_t vx = V[op_x(opcode)];
        memory[I] = vx / 100;
        memory[I+1] = (vx / 10) % 10;
        memory[I+2] = vx % 10;
        return EML_OK;
    }
    /*
     * Fx55 - LD [I], Vx:
     * Store registers V0 through Vx in memory starting at location I.
     */
    case 0x0055:
        for (int i = 0; i <= op_x(opcode); i++)
            memory[I+i] = V[i];
        I += op_x(opcode) + 1;
        return EML_OK;
    /*
     * Fx65 - LD Vx, [I]:
     * Read registers V0 through Vx from memory starting at location I.
     */
    case 0x0065:
        for (int i = 0; i <= op_x(opcode); i++)
            V[i] = memory[I+i];
        I += op_x(opcode) + 1;
        return EML_OK;
    default:
        return EML_UNK_OPC;
    }
}

/* Dump machine state to stdout */
static void chip8_dump() {
    fprintf(stdout, "PC: 0x%02X\n", PC);
    fprintf(stdout, "ST: 0x%02X\n", ST);
    fprintf(stdout, "DT: 0x%02X\n", DT);
    fprintf(stdout, "I: 0x%02X\n\n", I);

    for (int i = 0; i < 15; i++)
        fprintf(stdout, "V%X: 0x%02X\n", i, V[i]);

    fprintf(stdout, "\nSP: 0x%02X\n", SP);
    for (int i = 0; i < 16; i++)
        fprintf(stdout, "stack[%X]: 0x%04X\n", i, stack[i]);
}

static enum eml_stat chip8_cycle() {
    static enum eml_stat (*j_tbl1[]) (void) = {
        _b0, _b1, _b2, _b3, _b4, _b5, _b6, _b7,
        _b8, _b9, _bA, _bB, _bC, _bD, _bE, _bF
    };

    if (key_waiting)
        return EML_WAIT_KEY;

    if (PC+1 >= MEM_SIZE)
        return EML_PC_OVERFL;

    /* Fetch instruction */
    opcode = memory[PC] << 8 | memory[PC + 1];

    /* PC points to next instruction during instruction execution */
    PC += 2;

    /* Keep the old PC. (Instructions like JP change it) */
    prev_PC = PC;

    /* Interpret next instruction */
    enum eml_stat status = j_tbl1[(opcode & 0xF000) >> 12]();

    if (dbg_output)
        fprintf(stdout, "PC=%04u, SP=%02u, opcode=0x%04X\n",
                prev_PC, SP, opcode);

    if (brk_point_set && prev_PC == brk_point) /* Check if breakpoint reached */
        return EML_BRK_REACHED;

    return status;
}

void chip8_timer_dec() {
    if (ST > 0)
        ST--;
    if (DT > 0)
        DT--;
}

static void chip8_init() {
    memcpy(memory, chip8_fontset, sizeof chip8_fontset * sizeof(uint8_t));
}

bool load_program(char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Unable to read file %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    int max_prg_size = MEM_SIZE - RESERVED_MEM;
    if (fsize > max_prg_size) {
        fprintf(stderr,
                "Unable to load program %s: Too large (%ld, max: %db)\n",
                path, fsize, max_prg_size);
        fclose(f);
        return false;
    }
    fread(memory + RESERVED_MEM, fsize, 1, f);
    fclose(f);
    return true;
}

void display_redraw() {
    int32_t win_width;
    int32_t win_height;
    SDL_GetWindowSize(window, &win_width, &win_height);
    int32_t grid_w = win_width / DISP_W;
    int32_t grid_h = win_height / DISP_H;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    for (int i = 0; i < DISP_H; i++) {
        for (int j = 0; j < DISP_W; j++) {
            if (display[i*DISP_W + j] == 1)
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            else
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_Rect r = { j*grid_w, i*grid_h, grid_w, grid_h };
            SDL_RenderFillRect(renderer, &r);
        }
    }
    SDL_RenderPresent(renderer);
}

/* return true when we should terminate */
static bool event_handler(SDL_Event *event) {
    switch (event->type) {
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_MOVED ||
            event->window.event == SDL_WINDOWEVENT_EXPOSED ||
            event->window.event == SDL_WINDOWEVENT_RESTORED ||
            event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            display_redraw();
        }
        break;
    case SDL_QUIT:
        return true;
    case SDL_KEYDOWN:
        switch (event->key.keysym.sym) {
            case SDLK_ESCAPE:
                return true;
            case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
            case SDLK_q: case SDLK_w: case SDLK_e: case SDLK_r:
            case SDLK_a: case SDLK_s: case SDLK_d: case SDLK_f:
            case SDLK_y: case SDLK_x: case SDLK_c: case SDLK_v:
                keypad_pressed(event->key.keysym.sym);
            break;
        }
        break;
    case SDL_KEYUP:
        switch (event->key.keysym.sym) {
            case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
            case SDLK_q: case SDLK_w: case SDLK_e: case SDLK_r:
            case SDLK_a: case SDLK_s: case SDLK_d: case SDLK_f:
            case SDLK_y: case SDLK_x: case SDLK_c: case SDLK_v:
                keypad_released(event->key.keysym.sym);
            break;
        }
        break;
    }
    return false;
}

static int event_filter(void *data, SDL_Event *event) {
    (void) data;
    switch (event->type) {
    case SDL_WINDOWEVENT: case SDL_QUIT: case SDL_KEYDOWN: case SDL_KEYUP:
        return 1;
    }
    return 0;
}

static bool sdl_setup() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stdout, "SDL_Init error: %s\n", SDL_GetError());
		return false;
    }

    window = SDL_CreateWindow("Chip8", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, SDL_WIN_W, SDL_WIN_H,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);
    if (!window) {
		fprintf(stdout, "SDL_CreateWindow error: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
		fprintf(stdout, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
		SDL_Quit();
        return false;
    }

    /* the event filter makes sure only relevant events are queued */
    SDL_SetEventFilter(event_filter, NULL);
    return true;
}

static void prt_usage() {
    static const char *usage =
        "Usage: chip8 [file]\n\n"
        "  -h           Print this message and exit\n"
        "  -c           Set the clock speed (in Hz, default 1080 Hz)\n"
        "  -d           Enable debug output\n"
        "  -b [addr]    Set breakpoint at addr\n";
    fprintf(stdout, "%s", usage);
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "hc:db:")) != -1) {
        switch (opt) {
        case 'h':
            prt_usage();
            exit(EXIT_SUCCESS);
            break;
        case 'c':
            clock_speed = atoi(optarg);
            break;
        case 'd':
            dbg_output = true;
            break;
        case 'b':
            brk_point = atoi(optarg);
            brk_point_set = true;
            break;
        default:
            prt_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        prt_usage();
        fprintf(stderr, "\nError: Expected file name argument\n");
        exit(EXIT_FAILURE);
    }
    char *rom_file = argv[optind];
    fprintf(stdout, "Loading file: %s\n", rom_file);

    if (brk_point_set)
        fprintf(stdout, "Breakpoint: %u\n", brk_point);

    fprintf(stdout, "Clock speed: %d Hz\n", clock_speed);

    sdl_setup();
    atexit(SDL_Quit);

    chip8_init();
    display_redraw();

    /* load a program */
    if (!load_program(rom_file))
        return 1;

    /*
     * The main loop is run at 60 Hz, but the emulator run much faster.
     * Every iteration, a number of cycles are run such that the desired
     * clock speed is reached *on average*.
     *
     * The counters are decreased at a defined rate of 60 Hz.
     */
    SDL_Event event;
    struct timespec t_start = {0, 0};
    struct timespec t_end = {0, 0};
    struct timespec t_delta = {0, 0};
    int cycle_n = clock_speed / 60; /* num of cycles per main loop iteration */
    bool terminate = false;
    while (!terminate) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &t_start);

        bool redraw = false;
        bool fault = false;
        for (int i = 0; i < cycle_n && !fault; i++) {
            /* handle input events */
            while (SDL_PollEvent(&event)) {
                terminate = event_handler(&event);
            }

            /* run a Chip8 cycle */
            switch(chip8_cycle()) {
            case EML_REDRAW:
                redraw = true;
                break;
            case EML_UNK_OPC:
                fprintf(stderr, "Fault: Invalid opcode at PC=%u: 0x%04X\n",
                        PC, opcode);
                fault = true;
                terminate = true;
                break;
            case EML_STACK_OVERFL:
                fprintf(stderr, "Fault: Stack overflow at PC=%u\n", prev_PC);
                fault = true;
                terminate = true;
                break;
            case EML_STACK_UNDERFL:
                fprintf(stderr,
                        "Fault: Trying to pop from empty stack at PC=%u\n",
                        PC);
                fault = true;
                terminate = true;
                break;
            case EML_BRK_REACHED:
                fprintf(stdout, "Breakpoint reached\n");
                chip8_dump();
                fault = true;
                terminate = true;
                break;
            default:
                break;
            }
        }

        if (redraw)
            display_redraw();

        /* sleep if we have time left */
        clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
        t_delta.tv_nsec = t_end.tv_nsec - t_start.tv_nsec;
        if (t_delta.tv_nsec < _60HZ) {
            t_delta.tv_nsec = _60HZ - t_delta.tv_nsec;
            nanosleep(&t_delta, NULL);
        }

        chip8_timer_dec();
    }

    return 0;
}

