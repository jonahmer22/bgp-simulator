# Changelog

All notable changes to this project will be documented in this file.

This project adheres to [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and follows [Semantic Versioning](https://semver.org/) (0.x indicates initial development).

## [v1.01] - 2025-08-29
### Changed
- Arena has more safety gaurds.
- Arena attempts to alloc at least twice before faulting.
- The global arena does not have to be explicitly initialized anymore (must still be destroyed though).
- Compressed functionality into fewer lines.
- inlined all functions to allow for a (slight) performance increase.
### Added
- Safe gaurds to prevent missuse and redundant allocation attempts.
##### Global:
- `arenaInit()`: Initializes global arena with one empty BUFF_SIZE (1MB by default) block of memory.
- `arenaAlloc(size_t size)`: Drop in replacement for malloc() call, allocates to global arena
- `arenaReset()`: Resets the arena and frees all blocks of memory and wipes the origin block for reuse.
- `arenaAllocBuffsizeBlock()`: A special function that returns a BUFF_SIZE (1MB by default) block that is never to be used by the arena. Usefull for allocating blocks of memory for specific purposes that should not be mixed with other memory. 
- `arenaDestroy()`: Destroys the global arena and frees all memory associated with it.
##### Local:
- `arenaLocalInit(Arena *arena)`: Initializes a local arena with one empty BUFF_SIZE (1MB by default) block of memory.
- `arenaLocalAlloc(Arena *arena, size_t size)`: Drop in replacement for malloc() call, allocates to pointed to arena
- `arenaLocalReset(Arena *arena)`: Resets the pointed to arena and frees all blocks of memory and wipes the origin block for reuse.
- `arenaLocalAllocBuffsizeBlock(Arena *arena)`: A special function that returns a BUFF_SIZE (1MB by default) block that is never to be used by the arena. Usefull for allocating blocks of memory for specific purposes that should not be mixed with other memory. 
- `arenaLocalDestroy(Arena *arena)`: Destroys the pointed to arena and frees all memory associated with it.

---
