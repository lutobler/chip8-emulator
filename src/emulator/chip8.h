/**
 * Copyright (c) 2018, Lukas Tobler
 * GNU General Public License v3.0
 * (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
 */

#ifndef __CHIP8_H
#define __CHIP8_H

#include <stdint.h>
#include <stdbool.h>

/* Chip8 hardware parameters */
#define MEM_SIZE 4096
#define RESERVED_MEM 512
#define DISP_W 64
#define DISP_H 32
#define DISP_SIZE DISP_W*DISP_H
#define STACK_SIZE 16
#define _60HZ 16666667L /* (1/60) seconds in ns */

enum chip8_key {
    C8K_0 = 0x0, C8K_1 = 0x1, C8K_2 = 0x2, C8K_3 = 0x3,
    C8K_4 = 0x4, C8K_5 = 0x5, C8K_6 = 0x6, C8K_7 = 0x7,
    C8K_8 = 0x8, C8K_9 = 0x9, C8K_A = 0xA, C8K_B = 0xB,
    C8K_C = 0xC, C8K_D = 0xD, C8K_E = 0xE, C8K_F = 0xF,
    C8K_NONE = -1
};

/* Chip8 machine state */
struct chip8 {
    uint8_t memory[MEM_SIZE];           /* 4096 bytes of memory */
    uint8_t display[DISP_SIZE];         /* 64x32 pixel monochrome display */
    uint8_t V[16];                      /* V0 to VF data registers */
    uint8_t SP;                         /* Stack pointer (next free slot) */
    uint8_t DT;                         /* Delay timer */
    uint8_t ST;                         /* Sound timer */
    uint16_t PC;                        /* Program counter. Start at 0x200 */
    uint16_t I;                         /* Address register */
    uint16_t stack[STACK_SIZE];         /* Stack */
};

struct emulator {
    struct chip8 cpu;
    uint16_t opcode;                    /* Last read opcode */
    uint16_t prev_PC;                   /* Previous PC (for error output) */
    uint16_t keypad;                    /* Bitmap for pressed keys, 0-F */
    enum chip8_key last_key;            /* The last pressed key */
    int32_t brk_point;                  /* Curent breakpoint */
    int32_t clock_speed;                /* Clock speed in Hz */
    char *rom_file;                     /* File name of loaded rom */
    bool key_waiting;                   /* Emulator is wating for a key */
    bool dbg_output;                    /* Debug output flag */
    bool brk_point_set;                 /* Breakpoint enable flag */
    bool paused;                        /* Paused emulator state */
};

/* Emulator state after executing an instruction */
enum eml_stat {
    EML_OK,                             /* No errors during cycle */
    EML_REDRAW,                         /* Display redraw required */
    EML_BRK_REACHED,                    /* Breakpoint reached */
    EML_UNK_OPC,                        /* Error: Unknown opcode */
    EML_STACK_OVERFL,                   /* Error: Stack overflow */
    EML_STACK_UNDERFL,                  /* Error: Stack is empty */
    EML_PC_OVERFL                       /* Error: PC outside memory */
};

/* Emulator functions */
void emulator_init(struct emulator *eml);
enum eml_stat emulator_cycle(struct emulator *eml);
void emulator_timer_dec(struct emulator *eml);
bool emulator_load_program(struct emulator *eml, char *path);
void emulator_dump(struct emulator *eml);

#endif

