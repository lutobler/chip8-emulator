## Yet another Chip8 emulator and assembler

This is my attempt at making a Chip8 emulator (or interpreter, whatever you
want to call it) and an assembler.

Dependencies:

* POSIX 🙃
* [SDL2](https://www.libsdl.org/)
* [meson](http://mesonbuild.com/), [ninja](https://ninja-build.org/)

### Building

The meson build system is used:

```
meson build
cd build
ninja
```

