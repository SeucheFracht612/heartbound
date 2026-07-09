# 0001 Language and Build System

Decision:
C++23, CMake, Ninja, and vcpkg manifest mode.

Why:
The engine needs native performance, explicit ownership of memory and threading,
portable build configuration, and an IDE workflow that works cleanly in CLion.

Alternatives considered:
Unity/Unreal, Rust, C++20, custom build scripts.

Consequences:
The project owns more engine code directly, but gains control over save compatibility,
mod boundaries, voxel/world representations, and long-term architecture.

When to revisit:
Only if platform support or dependency requirements make the current toolchain
unworkable.
