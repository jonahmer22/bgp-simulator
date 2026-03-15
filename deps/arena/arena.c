#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

// If the platform is unix or apple assuming you have mmap
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define ARENA_HAVE_MMAP 1
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#else
#define ARENA_HAVE_MMAP 0
#endif

// =======
// private
// =======

// Global arena
static Arena* arena;

#if ARENA_HAVE_MMAP
static size_t arenaGetPageSize(void){
	static size_t pageSize = 0;
	if(pageSize == 0){
		long ps = sysconf(_SC_PAGESIZE);
		if(ps <= 0){
			pageSize = 4096;
		}
		else{
			pageSize = (size_t)ps;
		}
	}

	return pageSize;
}
#endif

// Initializes a MemBlock with an inline aligned buffer and returns a pointer to it
static inline MemBlock* memBlockInit(size_t size, size_t align){
	size = ROUND_UP(size, align);
	const size_t total = sizeof(MemBlock) + align - 1 + size;

	MemBlock *memBlock = NULL;
	size_t reserved = total;
	unsigned char fromMmap = 0;

#if ARENA_HAVE_MMAP
	const size_t pageSize = arenaGetPageSize();
	const size_t mapSize = ((total + pageSize - 1) / pageSize) * pageSize;
	void *mapped = mmap(NULL, mapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(mapped != MAP_FAILED){
		memBlock = (MemBlock*)mapped;
		reserved = mapSize;
		fromMmap = 1;
	}
#endif

	if(!memBlock){
		memBlock = malloc(total);
		if(!memBlock){
			perror("[FATAL]: Could not allocate MemBlock.");

			exit(10);
		}

		reserved = total;
		fromMmap = 0;
	}

	uintptr_t bufStart = (uintptr_t)((unsigned char*)memBlock + sizeof(MemBlock));
	uintptr_t aligned = (bufStart + (align - 1)) & ~((uintptr_t)align - 1);

	memBlock->buffer = (char*)aligned;

	memBlock->nextBlock = NULL;
	memBlock->head = 0;
	memBlock->size = size;
	memBlock->allocSize = reserved;
	memBlock->fromMmap = fromMmap;

	return memBlock;
}

// Adds a MemBlock to the linked list of MemBlocks 
static inline MemBlock* memBlockAdd(MemBlock *memBlock, size_t size){
	memBlock->nextBlock = memBlockInit(size, ARENA_ALIGN);

	return memBlock->nextBlock;
}

// Adds a MemBlock to the linked list of MemBlocks aligned to the size of BuffSize
static inline MemBlock* memBlockAddAlignedBuff(MemBlock *memBlock){
	memBlock->nextBlock = memBlockInit(BUFF_SIZE, ARENA_ALIGN);

	return memBlock->nextBlock;
}

// Destroys and frees a MemBlock returning a pointer to the next one in the list
static inline MemBlock* memBlockDestroy(MemBlock* memBlock){
	MemBlock *temp = memBlock->nextBlock;
	const size_t reserved = memBlock->allocSize;
	const unsigned char fromMmap = memBlock->fromMmap;
	
#if ARENA_HAVE_MMAP
	if(fromMmap){
		if(munmap(memBlock, reserved) != 0){
			perror("[FATAL]: Could not munmap MemBlock.");

			exit(17);
		}
	}
	else
#endif
	{
		free(memBlock);
	}
	
	return temp;
}

// =====
// local
// =====

// Initializes a Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the Arena
Arena* arenaLocalInit(void){
	Arena *larena = malloc(sizeof(Arena));
	if(!larena){
		perror("[FATAL]: Could not allocate Arena.");

		exit(12);
	}
	
	larena->numBlocks = 1;
	larena->head = memBlockInit(BUFF_SIZE, ARENA_ALIGN);
	larena->tail = larena->head;

	return larena;
}

// Destroys and frees all associated memory with a Arena passed in as an arguement
void arenaLocalDestroy(Arena *larena){
	if(!larena){
		perror("[FATAL]: Cannot destroy uninitialized arena.");

		exit(13);
	}

	MemBlock* temp = larena->head;
	for(; temp != NULL;){
		temp = memBlockDestroy(temp);
	}

	larena->head = NULL;
	larena->tail = NULL;

	larena->numBlocks = 0;

	free(larena);
	
	return;
}

// Frees all memory associated with arena passed as arguement except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the Arena passed as arguement, which is optional to use
Arena* arenaLocalReset(Arena *larena){
	if(!larena){
		perror("[FATAL]: Cannot reset uninitialized arena.");

		exit(14);
	}
	
	MemBlock* temp = larena->head->nextBlock;
	for(; temp != NULL;){
		temp = memBlockDestroy(temp);
	}

	larena->head->nextBlock = NULL;
	larena->tail = larena->head;
	larena->head->head = 0;
	larena->numBlocks = 1;

	return larena;
}

// Returns a pointer to the base of a block of memory numBytes in size
// increments all Arena head pointers and allocates extra memory if needed to the Arena passed as an arguement
void* arenaLocalAlloc(Arena *larena, size_t numBytes){
	if(!larena){
		perror("[FATAL]: Cannot allocate to uninitialized arena.");

		exit(15);
	}

	numBytes = ROUND_UP(numBytes, ARENA_ALIGN);

	if(numBytes > BUFF_SIZE){
		// add a block specifically for this big chunk of data
		larena->tail = memBlockAdd(larena->tail, numBytes);
		larena->numBlocks++;

		void* ptr = larena->tail->buffer + larena->tail->head;
		larena->tail->head += numBytes;

		return ptr;
	}
	else if(larena->tail->head + numBytes > larena->tail->size){
		// add on a new block and just add to there
		larena->tail = memBlockAdd(larena->tail, BUFF_SIZE);
		larena->numBlocks++;
		
		void* ptr = larena->tail->buffer + larena->tail->head;
		larena->tail->head += numBytes;

		return ptr;
	}
	else{
			// if we can just fit it in our current block
		void* ptr = larena->tail->buffer + larena->tail->head;
		larena->tail->head += numBytes;

		return ptr;
	}
}

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena passed as an arguement
void* arenaLocalAllocBuffsizeBlock(Arena *larena){
	if(!larena){
		perror("[FATAL]: Cannot allocate to uninitialized arena.");

		exit(16);
	}

	larena->tail = memBlockAddAlignedBuff(larena->tail);
	larena->numBlocks++;

	void* ptr = larena->tail->buffer + larena->tail->head;
	larena->tail->head = BUFF_SIZE;

	return ptr;
}

// Returns only whether a given arena has been created in the form of a int 1 = true, 0 = false
// an Arena* must be passed as an arguement
int arenaLocalIsInitialized(Arena* larena){
	return larena ? 1 : 0;
}

// ======
// global
// ======

// Initializes the global Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the global Arena, which is optional to use
Arena* arenaInit(void){
	if(!arena){
		arena = arenaLocalInit();
	}

	return arena;
}

// Destroys and frees all associated memory with the global Arena
void arenaDestroy(void){
	if(!arena){
		return;
	}

	arenaLocalDestroy(arena);
	arena = NULL;
}

// Frees all memory in the global arena except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the global Arena, which is optional to use
Arena* arenaReset(void){
	if(!arena){
		return arenaInit();
	}

	return arenaLocalReset(arena);
}

// Returns a pointer to the base of a block of memory numBytes in size
// increments all global Arena head pointers and allocates extra memory if needed
void* arenaAlloc(size_t numBytes){
	if(!arena){
		// because we know the arena instead of just printing an error and exiting we can create it for the user
		arenaInit();
	}

	return arenaLocalAlloc(arena, numBytes);    
}

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena
void* arenaAllocBuffsizeBlock(void){
	if(!arena){
		// can initialize the arena for the user as we know which one they intend to use
		arenaInit();
	}
	
	return arenaLocalAllocBuffsizeBlock(arena);
}

// Returns only whether the arena has been created in the form of a int 1 = true, 0 = false
int arenaIsInitialized(void){
	return arenaLocalIsInitialized(arena);
}
