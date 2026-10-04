"""Prefix the SCons compilers with ccache when it is available.

Covers project sources, the Arduino core and libraries. The ESP-IDF components
are compiled by CMake instead, which picks up ccache via
`board_build.cmake_extra_args = -DCCACHE_ENABLE=1`.
"""

from shutil import which

Import("env")

ccache = which("ccache")

if not ccache:
    print("ccache not found, building without it")
else:
    for tool in ("CC", "CXX", "AS"):
        compiler = env.get(tool)
        if compiler and "ccache" not in compiler:
            env.Replace(**{tool: f"{ccache} {compiler}"})
