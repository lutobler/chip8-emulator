/**
 * Copyright (c) 2018, Lukas Tobler
 * GNU General Public License v3.0
 * (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "chip8.h"

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

/* Test if a key is pressed */
static inline bool keypad_is_pressed(struct emulator *eml, enum chip8_key key) {
    return eml->keypad & (1 << key);
}

/* Extract the x nibble from an opcode */
static inline uint8_t op_x(uint16_t opcode) { return (opcode >> 8) & 0xF; }

/* Extract the y nibble from an opcode */
static inline uint8_t op_y(uint16_t opcode) { return (opcode >> 4) & 0xF; }

/* Extract the kk byte from an opcode */
static inline uint8_t op_kk(uint16_t opcode) { return opcode & 0xFF; }

/* Extract the nnn 12-bit number from an opcode */
static inline uint16_t op_nnn(uint16_t opcode) { return opcode & 0xFFF; }

/* 00E0 - CLS: Clear the display. */
static inline enum eml_stat instr_CLS(uint16_t opcode, struct emulator *eml) {
    (void) opcode;
    memset(&eml->cpu.display, 0, DISP_SIZE);
    return EML_REDRAW;
}

/* 00EE - RET: Return from a subroutine. */
static inline enum eml_stat instr_RET(uint16_t opcode, struct emulator *eml) {
    (void) opcode;
    if (eml->cpu.SP == 0) {
        return EML_STACK_UNDERFL;
    }
    eml->cpu.PC = eml->cpu.stack[--eml->cpu.SP];
    return EML_OK;
}

/* 00EE - RET: Return from a subroutine. */
static inline enum eml_stat instr_JP_nnn(uint16_t opcode, struct emulator *eml) {
    eml->cpu.PC = op_nnn(opcode);
    return EML_OK;
}

/* 2nnn - CALL addr: Call subroutine at nnn. */
static inline enum eml_stat instr_CALL_nnn(uint16_t opcode, struct emulator *eml) {
    if (eml->cpu.SP == STACK_SIZE) {
        return EML_STACK_OVERFL;
    }
    eml->cpu.stack[eml->cpu.SP++] = eml->cpu.PC;
    eml->cpu.PC = op_nnn(opcode);
    return EML_OK;
}

/* 3xkk - SE Vx, byte: Skip next instruction if Vx = kk. */
static inline enum eml_stat instr_SE_Vx_kk(uint16_t opcode, struct emulator *eml) {
    if (eml->cpu.V[op_x(opcode)] == op_kk(opcode)) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/* 4xkk - SNE Vx, byte: Skip next instruction if Vx != kk. */
static inline enum eml_stat instr_SNE_Vx_kk(uint16_t opcode, struct emulator *eml) {
    if (eml->cpu.V[op_x(opcode)] != op_kk(opcode)) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/* 5xy0 - SE Vx, Vy: Skip next instruction if Vx = Vy. */
static inline enum eml_stat instr_SE_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    if (eml->cpu.V[op_x(opcode)] == eml->cpu.V[op_y(opcode)]) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/* 6xkk - LD Vx, byte: Set Vx = kk. */
static inline enum eml_stat instr_LD_Vx_kk(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] = op_kk(opcode);
    return EML_OK;
}

/* 7xkk - ADD Vx, byte: Set Vx = Vx + kk. */
static inline enum eml_stat instr_ADD_Vx_kk(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] += op_kk(opcode);
    return EML_OK;
}

/* 8xy0 - LD Vx, Vy: Set Vx = Vy. */
static inline enum eml_stat instr_LD_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] = eml->cpu.V[op_y(opcode)];
    return EML_OK;
}

/* 8xy1 - OR Vx, Vy: Set Vx = Vx OR Vy. */
static inline enum eml_stat instr_OR_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] |= eml->cpu.V[op_y(opcode)];
    return EML_OK;
}

/* 8xy2 - AND Vx, Vy: Set Vx = Vx AND Vy. */
static inline enum eml_stat instr_AND_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] &= eml->cpu.V[op_y(opcode)];
    return EML_OK;
}

/* 8xy3 - XOR Vx, Vy: Set Vx = Vx XOR Vy. */
static inline enum eml_stat instr_XOR_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] ^= eml->cpu.V[op_y(opcode)];
    return EML_OK;
}

/* 8xy4 - ADD Vx, Vy: Set Vx = Vx + Vy, set eml->cpu.V[0xF] = carry. */
static inline enum eml_stat instr_ADD_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    uint16_t s = (uint16_t) eml->cpu.V[op_x(opcode)] + (uint16_t) eml->cpu.V[op_y(opcode)];
    eml->cpu.V[0xF] = (s > 0xFF) ? 1 : 0;
    eml->cpu.V[op_x(opcode)] = s & 0xFF;
    return EML_OK;
}

/* 8xy5 - SUB Vx, Vy: Set Vx = Vx - Vy, set eml->cpu.V[0xF] = NOT borrow. */
static inline enum eml_stat instr_SUB_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[0xF] = (eml->cpu.V[op_x(opcode)] > eml->cpu.V[op_y(opcode)]) ? 1 : 0;
    eml->cpu.V[op_x(opcode)] -= eml->cpu.V[op_y(opcode)];
    return EML_OK;
}

/* 8xy6 - SHR Vx {, Vy}: Set Vx = Vx SHR 1. */
static inline enum eml_stat instr_SHR_Vx__Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[0xF] = eml->cpu.V[op_x(opcode)] & 0x01;
    eml->cpu.V[op_x(opcode)] = eml->cpu.V[op_x(opcode)] >> 1;
    return EML_OK;
}

/* 8xy7 - SUBN Vx, Vy: Set Vx = Vy - Vx, set eml->cpu.V[0xF] = NOT borrow. */
static inline enum eml_stat instr_SUBN_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[0xF] = (eml->cpu.V[op_x(opcode)] < eml->cpu.V[op_y(opcode)]) ? 1 : 0;
    eml->cpu.V[op_x(opcode)] = eml->cpu.V[op_y(opcode)] - eml->cpu.V[op_x(opcode)];
    return EML_OK;
}

/* 8xyE - SHL Vx {, Vy}: Set Vx = Vx SHL 1. */
static inline enum eml_stat instr_SHL_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[0xF] = (eml->cpu.V[op_x(opcode)] & 0x80) >> 7;
    eml->cpu.V[op_x(opcode)] = eml->cpu.V[op_x(opcode)] << 1;
    return EML_OK;
}

/* 9xy0 - SNE Vx, Vy: Skip next instruction if Vx != Vy. */
static inline enum eml_stat instr_SNE_Vx_Vy(uint16_t opcode, struct emulator *eml) {
    if (eml->cpu.V[op_x(opcode)] != eml->cpu.V[op_y(opcode)]) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/* Annn - LD I, addr: Set I = nnn. */
static inline enum eml_stat instr_LD_I_nnn(uint16_t opcode, struct emulator *eml) {
    eml->cpu.I = op_nnn(opcode);
    return EML_OK;
}

/* Bnnn - JP V0, addr: Jump to location nnn + V0. */
static inline enum eml_stat instr_JP_V0_nnn(uint16_t opcode, struct emulator *eml) {
    eml->cpu.PC = op_nnn(opcode) + eml->cpu.V[0];
    return EML_OK;
}

/* Cxkk - RND Vx, byte: Set Vx = random byte AND kk. */
static inline enum eml_stat instr_RND_Vx_kk(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] = rand() & op_kk(opcode);
    return EML_OK;
}

/*
 * Dxyn - DRW Vx, Vy, nibble:
 * Display n-byte sprite starting at memory location I at (Vx, Vy),
 * set eml->cpu.V[0xF] = collision.
 */
static inline enum eml_stat instr_DRW_Vx_Vy_n(uint16_t opcode, struct emulator *eml) {
    uint8_t n = opcode & 0xF;
    uint8_t x = eml->cpu.V[op_x(opcode)];
    uint8_t y = eml->cpu.V[op_y(opcode)];
    uint8_t collide = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 8; j++) {
            uint32_t d_idx = ((i + y) % DISP_H)*DISP_W + (x + j) % DISP_W;
            uint8_t sprt_bit = (eml->cpu.memory[eml->cpu.I + i] >> (7 - j)) & 0x1;
            if (eml->cpu.display[d_idx] == 1 && sprt_bit == 1) {
                collide = 1;
            }
            eml->cpu.display[d_idx] ^= sprt_bit;
        }
    }

    eml->cpu.V[0xF] = collide;
    return EML_REDRAW;
}

/*
 * Ex9E - SKP Vx:
 * Skip next instruction if key with the value of Vx is pressed.
 */
static inline enum eml_stat instr_SKP_Vx(uint16_t opcode, struct emulator *eml) {
    if (keypad_is_pressed(eml, eml->cpu.V[op_x(opcode)])) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/*
 * ExA1 - SKNP Vx:
 * Skip next instruction if key with the value of Vx is not pressed.
 */
static inline enum eml_stat instr_SKNP_Vx(uint16_t opcode, struct emulator *eml) {
    if (!keypad_is_pressed(eml, eml->cpu.V[op_x(opcode)])) {
        eml->cpu.PC += 2;
    }
    return EML_OK;
}

/* Fx07 - LD Vx, DT: Set Vx = delay timer value. */
static inline enum eml_stat instr_LD_Vx_DT(uint16_t opcode, struct emulator *eml) {
    eml->cpu.V[op_x(opcode)] = eml->cpu.DT;
    return EML_OK;
}

/* Fx0A - LD Vx, K: Wait for a key press, store the value of the key in Vx. */
static inline enum eml_stat instr_LD_Vx_K(uint16_t opcode, struct emulator *eml) {
    if (eml->last_key == C8K_NONE) { /* start waiting-for-key process */
        eml->key_waiting = true;
        return EML_OK;
    } else { /* stop waiting */
        eml->cpu.V[op_x(opcode)] = eml->last_key;
        eml->last_key = C8K_NONE;
        return EML_OK;
    }
}

/* Fx15 - LD DT, Vx: Set delay timer = Vx. */
static inline enum eml_stat instr_LD_DT_Vx(uint16_t opcode, struct emulator *eml) {
    eml->cpu.DT = eml->cpu.V[op_x(opcode)];
    return EML_OK;
}

/* Fx18 - LD ST, Vx: Set sound timer = Vx. */
static inline enum eml_stat instr_LD_ST_Vx(uint16_t opcode, struct emulator *eml) {
    eml->cpu.ST = eml->cpu.V[op_x(opcode)];
    return EML_OK;
}

/* Fx1E - ADD I, Vx: Set I = I + Vx. */
static inline enum eml_stat instr_ADD_I_Vx(uint16_t opcode, struct emulator *eml) {
    eml->cpu.I += eml->cpu.V[op_x(opcode)];
    eml->cpu.V[0xF] = (eml->cpu.I > 0xFFF) ? 1 : 0; /* undocumented feature */
    return EML_OK;
}

/* Fx29 - LD F, Vx: Set I = location of sprite for digit Vx. */
static inline enum eml_stat instr_LD_F_Vx(uint16_t opcode, struct emulator *eml) {
    eml->cpu.I = eml->cpu.V[op_x(opcode)] * 5;
    return EML_OK;
}

/*
 * Fx33 - LD B, Vx:
 * Store BCD representation of Vx in memory locations I, I+1, and I+2.
 */
static inline enum eml_stat instr_LD_B_Vx(uint16_t opcode, struct emulator *eml) {
    uint8_t vx = eml->cpu.V[op_x(opcode)];
    eml->cpu.memory[eml->cpu.I] = vx / 100;
    eml->cpu.memory[eml->cpu.I+1] = (vx / 10) % 10;
    eml->cpu.memory[eml->cpu.I+2] = vx % 10;
    return EML_OK;
}

/*
 * Fx55 - LD [I], Vx:
 * Store registers V0 through Vx in memory starting at location I.
 */
static inline enum eml_stat instr_LD_I_Vx_multi(uint16_t opcode, struct emulator *eml) {
    for (int i = 0; i <= op_x(opcode); i++) {
        eml->cpu.memory[eml->cpu.I+i] = eml->cpu.V[i];
    }
    return EML_OK;
}

/*
 * Fx65 - LD Vx, [I]:
 * Read registers V0 through Vx from memory starting at location I.
 */
static inline enum eml_stat instr_LD_Vx_I_multi(uint16_t opcode, struct emulator *eml) {
    for (int i = 0; i <= op_x(opcode); i++) {
        eml->cpu.V[i] = eml->cpu.memory[eml->cpu.I+i];
    }
    return EML_OK;
}

void emulator_init(struct emulator *eml) {
    memset(eml, 0, sizeof *eml);
    memcpy(&eml->cpu.memory, chip8_fontset, sizeof chip8_fontset * sizeof(uint8_t));
    eml->last_key = C8K_NONE;
    eml->clock_speed = 1080;
    eml->cpu.PC = 0x200;
}

void emulator_timer_dec(struct emulator *eml) {
    if (eml->cpu.ST > 0) {
        eml->cpu.ST--;
    }
    if (eml->cpu.DT > 0) {
        eml->cpu.DT--;
    }
}

enum eml_stat emulator_cycle(struct emulator *eml) {
    struct chip8 *cpu = &eml->cpu;

    if (eml->key_waiting) {
        return EML_OK;
    }
    if (eml->cpu.PC+1 >= MEM_SIZE) {
        return EML_PC_OVERFL;
    }

    /* Fetch instruction */
    eml->opcode = cpu->memory[cpu->PC] << 8 | cpu->memory[cpu->PC + 1];

    /* Keep the old PC. (Instructions like JP change it) */
    eml->prev_PC = cpu->PC;

    /* PC points to next instruction during instruction execution */
    cpu->PC += 2;

    /* Interpret next instruction */
    enum eml_stat status = EML_UNK_OPC;
    switch ((eml->opcode & 0xF000) >> 12) {
    case 0x0:
        switch (eml->opcode & 0xFF) {
        case 0xE0: status = instr_CLS(eml->opcode, eml); goto endcase;
        case 0xEE: status = instr_RET(eml->opcode, eml); goto endcase;
        }
    case 0x1: status = instr_JP_nnn(eml->opcode, eml); goto endcase;
    case 0x2: status = instr_CALL_nnn(eml->opcode, eml); goto endcase;
    case 0x3: status = instr_SE_Vx_kk(eml->opcode, eml); goto endcase;
    case 0x4: status = instr_SNE_Vx_kk(eml->opcode, eml); goto endcase;
    case 0x5: status = instr_SE_Vx_Vy(eml->opcode, eml); goto endcase;
    case 0x6: status = instr_LD_Vx_kk(eml->opcode, eml); goto endcase;
    case 0x7: status = instr_ADD_Vx_kk(eml->opcode, eml); goto endcase;
    case 0x8:
        switch (eml->opcode & 0xF) {
        case 0x0: status = instr_LD_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x1: status = instr_OR_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x2: status = instr_AND_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x3: status = instr_XOR_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x4: status = instr_ADD_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x5: status = instr_SUB_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0x6: status = instr_SHR_Vx__Vy(eml->opcode, eml); goto endcase;
        case 0x7: status = instr_SUBN_Vx_Vy(eml->opcode, eml); goto endcase;
        case 0xE: status = instr_SHL_Vx_Vy(eml->opcode, eml); goto endcase;
        }
    case 0x9: status = instr_SNE_Vx_Vy(eml->opcode, eml); goto endcase;
    case 0xA: status = instr_LD_I_nnn(eml->opcode, eml); goto endcase;
    case 0xB: status = instr_JP_V0_nnn(eml->opcode, eml); goto endcase;
    case 0xC: status = instr_RND_Vx_kk(eml->opcode, eml); goto endcase;
    case 0xD: status = instr_DRW_Vx_Vy_n(eml->opcode, eml); goto endcase;
    case 0xE:
        switch (eml->opcode & 0xFF) {
        case 0x9E: status = instr_SKP_Vx(eml->opcode, eml); goto endcase;
        case 0xA1: status = instr_SKNP_Vx(eml->opcode, eml); goto endcase;
        }
    case 0xF:
        switch (eml->opcode & 0xFF) {
        case 0x07: status = instr_LD_Vx_DT(eml->opcode, eml); goto endcase;
        case 0x0A: status = instr_LD_Vx_K(eml->opcode, eml); goto endcase;
        case 0x15: status = instr_LD_DT_Vx(eml->opcode, eml); goto endcase;
        case 0x18: status = instr_LD_ST_Vx(eml->opcode, eml); goto endcase;
        case 0x1E: status = instr_ADD_I_Vx(eml->opcode, eml); goto endcase;
        case 0x29: status = instr_LD_F_Vx(eml->opcode, eml); goto endcase;
        case 0x33: status = instr_LD_B_Vx(eml->opcode, eml); goto endcase;
        case 0x55: status = instr_LD_I_Vx_multi(eml->opcode, eml); goto endcase;
        case 0x65: status = instr_LD_Vx_I_multi(eml->opcode, eml); goto endcase;
        }
    }
endcase:

    if (eml->dbg_output) {
        fprintf(stdout, "PC=%04u, SP=%02u, opcode=0x%04X\n",
                eml->prev_PC, cpu->SP, eml->opcode);
    }

    /* Check if breakpoint reached */
    if (eml->brk_point_set && eml->prev_PC == eml->brk_point) {
        return EML_BRK_REACHED;
    }

    return status;
}

bool emulator_load_program(struct emulator *eml, char *path) {
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
    fread(eml->cpu.memory + RESERVED_MEM, fsize, 1, f);
    fclose(f);
    return true;
}

void emulator_dump(struct emulator *eml) {
    fprintf(stdout, "PC: 0x%02X\n", eml->cpu.PC);
    fprintf(stdout, "ST: 0x%02X\n", eml->cpu.ST);
    fprintf(stdout, "DT: 0x%02X\n", eml->cpu.DT);
    fprintf(stdout, "I: 0x%02X\n\n", eml->cpu.I);

    for (int i = 0; i < 16; i++) {
        fprintf(stdout, "V%X: 0x%02X\n", i, eml->cpu.V[i]);
    }

    fprintf(stdout, "\nSP: 0x%02X\n", eml->cpu.SP);
    for (int i = 0; i < 16; i++) {
        fprintf(stdout, "stack[%X]: 0x%04X\n", i, eml->cpu.stack[i]);
    }
}

