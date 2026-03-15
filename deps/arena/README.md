# arena
A basic, no dependency, shunting arena to make memory management cleaner, sequential and simpler for C development.

## Preamble
This arena implementation is purposefully meant to be a simple and small library for the source to be included in any project and be incorperated as simply as possible.

It is strongly recommended you read both this [README.md](README.md) and through the source of this library before using it. It is very short and fairly straightforward and will help you in understanding how to best utilize it.

If you have any suggestions or improvements to make please feel free to do so or contact me!

Throughout this readme there will be references to `BUFF_SIZE`. This simply means the size of the memory buffer in the arena which is 1MB by default. This can be changed by editing the value in [arena.h](./arena.h) at the top of the file in the definition section.

## Functions
Functions are split into two seperate catagories based off of functionality. For ever function in one catagory there is a "sister function" in the other section by a similar name that performs the same actions.
### Global Arena Functions
All the below functions apply to "the global arena" which is an internally stored arena in the library, this is to make basic single threaded programs easier to understand and simpler to switch over into this arena as the arguements to allocate memory have the same arity and footprint as `malloc()`.
- `Arena* arenaInit()`: Initializes the global Arena with an empty `BUFF_SIZE` block of memory. Optionally returns a pointer to the global Arena.
- `void arenaDestroy()`: Destroys and frees all associated memory with the global Arena.
- `Arena* arenaReset()`: Frees all memory in the global arena except the original `BUFF_SIZE` MemBlock and resets all heads to the base of the block. Optionally returns a pointer to the global Arena.
- `void* arenaAlloc(size_t numBytes)`: Returns a pointer to the base of a block of memory `numBytes` in size. Increments all global Arena head pointers and allocates extra memory if needed
- `void* arenaAllocBuffsizeBlock()`: Allocates a `BUFF_SIZE` block of memory. A pointer is returned to the base of the block of memory and it is marked as full to the Arena
### Local Arena Functions
All the below functions apply to "local arenas". Local arenas are simply arenas managed by some part of code other than the arena library itself. Because of that fact all the below functions act identically to the ones above but also require a pointer to the arena in which to perform the action.
- `Arena* arenaLocalInit()`: Initializes a Arena with an empty `BUFF_SIZE` block of memory. Returns a pointer to the Arena.
- `void arenaLocalDestroy(Arena *larena)`: Destroys and frees all associated memory with a Arena passed in as an arguement.
- `Arena* arenaLocalReset(Arena *larena)`: Frees all memory associated with arena passed as arguement except the original `BUFF_SIZE` block of memory and resets all heads to the base of the block. Optionally returns a pointer to the Arena passed as arguement.
- `void* arenaLocalAlloc(Arena *larena, size_t numBytes)`: Returns a pointer to the base of a block of memory `numBytes` in size. Increments all Arena head pointers and allocates extra memory if needed to the Arena passed as an arguement.
- `void* arenaLocalAllocBuffsizeBlock(Arena *larena)`: Allocates a `BUFF_SIZE` block of memory. A pointer is returned to the base of the block of memory and it is marked as full to the Arena passed as an arguement.

---