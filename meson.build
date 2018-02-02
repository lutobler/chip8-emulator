project('chip8', 'c', default_options : ['c_std=c99'])
inc_dir = include_directories('include')

# Chip8 emulator
emulator_src = ['src/chip8.c']
sdl2 = dependency('SDL2')
executable('chip8-eml', emulator_src, dependencies : sdl2)

