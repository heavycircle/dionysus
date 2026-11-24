cmake_minimum_required(VERSION 3.20)
# Opts out of the SDK's automatic setup before main() (docs/sdk/runtime.md); it
# has to be set before project(), which is where dino adds the SDK.
set(DINO_NO_AUTO_RUNTIME ON)
project(DinoRuntimeExample C)

dino_executable(runtime_example main.c)
