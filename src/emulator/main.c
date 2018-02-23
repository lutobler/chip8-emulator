/**
 * Copyright (c) 2018, Lukas Tobler
 * GNU General Public License v3.0
 * (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <getopt.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "chip8.h"

/* SDL constants */
#define SDL_WIN_W 640
#define SDL_WIN_H 320
#define OVERLAY_ALPHA 190
#define OVERLAY_FONTSIZE 25

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static struct emulator eml;

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_Font *ttf_sans;
static SDL_Surface *ttf_eml_name;
static SDL_Surface *ttf_rom_name;
static SDL_Surface *ttf_clk_speed;
static SDL_Texture *tex_eml_name;
static SDL_Texture *tex_rom_name;
static SDL_Texture *tex_clk_speed;
static SDL_Rect r_eml;
static SDL_Rect r_rom;
static SDL_Rect r_clk;
static SDL_Rect r_box;
static bool overlay_enabled = false;

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
static inline void keypad_pressed(struct emulator *eml, SDL_Keycode key) {
    eml->keypad |= 1 << (sdlk_to_c8k(key));
    if (eml->key_waiting) {
        eml->last_key = sdlk_to_c8k(key);
        eml->key_waiting = false;
    }
}

/* Set a key on the keypad as released */
static inline void keypad_released(struct emulator *eml, SDL_Keycode key) {
    eml->keypad ^= 1 << (sdlk_to_c8k(key));
}

/* update the overlay textures */
void update_overlay() {
    static SDL_Color fg = {255, 255, 255, 255};

    /* overlay text */
    char eml_name[128] = {0};
    char rom_name[128] = {0};
    char clk_speed[128] = {0};

    snprintf((char *) &eml_name, 128, "Chip8%s", eml.paused ? " (paused)" : "");
    snprintf((char *) &rom_name, 128, "Rom: %s", eml.rom_file);
    snprintf((char *) &clk_speed, 128, "Clock speed: %d Hz", eml.clock_speed);
    eml_name[127] = '\0';
    rom_name[127] = '\0';
    clk_speed[127] = '\0';

    ttf_eml_name = TTF_RenderText_Blended(ttf_sans, eml_name, fg);
    ttf_rom_name = TTF_RenderText_Blended(ttf_sans, rom_name, fg);
    ttf_clk_speed = TTF_RenderText_Blended(ttf_sans, clk_speed, fg);

    tex_eml_name = SDL_CreateTextureFromSurface(renderer, ttf_eml_name);
    tex_rom_name = SDL_CreateTextureFromSurface(renderer, ttf_rom_name);
    tex_clk_speed = SDL_CreateTextureFromSurface(renderer, ttf_clk_speed);

    r_eml.x = 10;
    r_eml.y = 5;
    r_eml.w = ttf_eml_name->w;
    r_eml.h = ttf_eml_name->h;

    r_rom.x = 10;
    r_rom.y = r_eml.h + 5;
    r_rom.w = ttf_rom_name->w;
    r_rom.h = ttf_rom_name->h;

    r_clk.x = 10;
    r_clk.y = r_eml.h + r_rom.h + 5;
    r_clk.w = ttf_clk_speed->w;
    r_clk.h = ttf_clk_speed->h;

    SDL_SetTextureBlendMode(tex_eml_name, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(tex_rom_name, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(tex_clk_speed, SDL_BLENDMODE_BLEND);

    SDL_SetTextureAlphaMod(tex_eml_name, OVERLAY_ALPHA);
    SDL_SetTextureAlphaMod(tex_rom_name, OVERLAY_ALPHA);
    SDL_SetTextureAlphaMod(tex_clk_speed, OVERLAY_ALPHA);

    /* overlay box */
    int maxw = MAX(MAX(r_eml.w, r_rom.w), r_clk.w);
    int sumh = r_eml.h + r_rom.h + r_clk.h;
    r_box.x = 0;
    r_box.y = 0;
    r_box.w = maxw + 20;
    r_box.h = sumh + 10;
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
            if (eml.cpu.display[i*DISP_W + j] == 1)
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            else
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_Rect r = { j*grid_w, i*grid_h, grid_w, grid_h };
            SDL_RenderFillRect(renderer, &r);
        }
    }

    if (overlay_enabled) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 94, 94, 94, OVERLAY_ALPHA);
        SDL_RenderFillRect(renderer, &r_box);

        SDL_RenderCopy(renderer, tex_eml_name, NULL, &r_eml);
        SDL_RenderCopy(renderer, tex_rom_name, NULL, &r_rom);
        SDL_RenderCopy(renderer, tex_clk_speed, NULL, &r_clk);
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
                keypad_pressed(&eml, event->key.keysym.sym);
            break;
        }
        break;
    case SDL_KEYUP:
        switch (event->key.keysym.sym) {
            case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
            case SDLK_q: case SDLK_w: case SDLK_e: case SDLK_r:
            case SDLK_a: case SDLK_s: case SDLK_d: case SDLK_f:
            case SDLK_y: case SDLK_x: case SDLK_c: case SDLK_v:
                keypad_released(&eml, event->key.keysym.sym);
                break;
            case SDLK_i:
                eml.clock_speed += 60;
                update_overlay();
                display_redraw();
                break;
            case SDLK_u:
                if (eml.clock_speed > 60) {
                    eml.clock_speed -= 60;
                }
                update_overlay();
                display_redraw();
                break;
            case SDLK_o:
                overlay_enabled = !overlay_enabled;
                display_redraw();
                break;
            case SDLK_p:
                eml.paused = !eml.paused;
                if (eml.paused) {
                    overlay_enabled = true;
                } else {
                    overlay_enabled = false;
                }
                update_overlay();
                display_redraw();
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

    /* SDL_ttf */
    TTF_Init();
    ttf_sans = TTF_OpenFont("Inconsolata-Bold.ttf", OVERLAY_FONTSIZE);

    /* the event filter makes sure only relevant events are queued */
    SDL_SetEventFilter(event_filter, NULL);
    return true;
}

void sdl_cleanup() {
    SDL_FreeSurface(ttf_eml_name);
    SDL_FreeSurface(ttf_rom_name);
    SDL_FreeSurface(ttf_clk_speed);
    SDL_DestroyTexture(tex_eml_name);
    SDL_DestroyTexture(tex_rom_name);
    SDL_DestroyTexture(tex_clk_speed);
    TTF_Quit();
    SDL_Quit();
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
    emulator_init(&eml);

    int opt;
    while ((opt = getopt(argc, argv, "hc:db:")) != -1) {
        switch (opt) {
        case 'h':
            prt_usage();
            exit(EXIT_SUCCESS);
            break;
        case 'c':
            eml.clock_speed = atoi(optarg);
            break;
        case 'd':
            eml.dbg_output = true;
            break;
        case 'b':
            eml.brk_point = atoi(optarg);
            eml.brk_point_set = true;
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
    eml.rom_file = argv[optind];
    fprintf(stdout, "Loading file: %s\n", eml.rom_file);

    if (eml.brk_point_set) {
        fprintf(stdout, "Breakpoint: %d\n", eml.brk_point);
    }

    fprintf(stdout, "Clock speed: %d Hz\n", eml.clock_speed);

    sdl_setup();
    update_overlay();
    display_redraw();

    /* load a program */
    if (!emulator_load_program(&eml, eml.rom_file)) {
        return 1;
    }

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
    bool terminate = false;
    while (!terminate) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &t_start);

        int cycle_n = eml.clock_speed / 60; /* num of cycles per main loop iteration */
        bool redraw = false;
        bool fault = false;
        for (int i = 0; i < cycle_n && !fault; i++) {
            /* handle input events */
            while (SDL_PollEvent(&event)) {
                terminate = event_handler(&event);
            }

            if (eml.paused) {
                break;
            }

            /* run a Chip8 cycle */
            switch(emulator_cycle(&eml)) {
            case EML_REDRAW:
                redraw = true;
                break;
            case EML_UNK_OPC:
                fprintf(stderr, "Fault: Invalid opcode at PC=%u: 0x%04X\n",
                        eml.prev_PC, eml.opcode);
                fault = true;
                terminate = true;
                break;
            case EML_STACK_OVERFL:
                fprintf(stderr, "Fault: Stack overflow at PC=%u\n", eml.prev_PC);
                fault = true;
                terminate = true;
                break;
            case EML_STACK_UNDERFL:
                fprintf(stderr,
                        "Fault: Trying to pop from empty stack at PC=%u\n",
                        eml.cpu.PC);
                fault = true;
                terminate = true;
                break;
            case EML_BRK_REACHED:
                fprintf(stdout, "Breakpoint reached\n");
                emulator_dump(&eml);
                fault = true;
                terminate = true;
                break;
            default:
                break;
            }
        }

        if (redraw) {
            display_redraw();
        }

        /* sleep if we have time left */
        clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
        t_delta.tv_nsec = t_end.tv_nsec - t_start.tv_nsec;
        if (t_delta.tv_nsec < _60HZ) {
            t_delta.tv_nsec = _60HZ - t_delta.tv_nsec;
            nanosleep(&t_delta, NULL);
        }

        emulator_timer_dec(&eml);
    }

    sdl_cleanup();
    return 0;
}

