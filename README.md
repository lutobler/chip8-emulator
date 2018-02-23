## Yet another Chip8 emulator

This is my attempt at making a Chip8 emulator (or interpreter, whatever you
want to call it).

Dependencies:

* POSIX 🙃
* [SDL2](https://www.libsdl.org/)
* [CMake](https://cmake.org/)

## Key bindings

Key | Action
----|-------
`o` | Toggle overlay
`p` | Pause/unpause
`u` | Decrease clock speed
`i` | Increase clock speed

### Building

The CMake build system is used:

```
mkdir build
cd build
cmake ..
make
```

