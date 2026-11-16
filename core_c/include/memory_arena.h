/*
 * memory_arena.h - Fixed-size linear (bump) arena allocator.
 *
 * Why: a game loop that calls malloc/free every frame fragments the heap and
 * produces unpredictable frame times. The arena owns ONE caller-provided
 * block (usually a static buffer), hands out aligned slices in O(1) by
 * bumping an offset, and frees everything at once with arena_reset().
 * Per-frame scratch memory uses arena_mark()/arena_rewind() so long-lived
 * allocations made at load time stay valid.
 *
 * The arena never calls malloc itself.
 */
#ifndef MEMORY_ARENA_H
#define MEMORY_ARENA_H

#include <stddef.h>

#define ARENA_DEFAULT_ALIGNMENT 16u
#define ARENA_KB(n) ((size_t)(n) * 1024u)

typedef struct MemoryArena {
    unsigned char *base;      /* start of the backing block */
    size_t capacity;          /* bytes in the backing block */
    size_t offset;            /* current bump position */
    size_t peak;              /* high-water mark (survives reset) */
    size_t alloc_count;       /* allocations since the last reset */
    size_t failed_allocs;     /* allocations rejected for lack of space */
} MemoryArena;

/* Saved position for scoped/per-frame scratch allocations. */
typedef struct ArenaMarker {
    size_t offset;
    size_t alloc_count;
} ArenaMarker;

void   arena_init(MemoryArena *arena, void *backing, size_t capacity);

/* Returns zeroed memory aligned to ARENA_DEFAULT_ALIGNMENT, or NULL when full. */
void  *arena_alloc(MemoryArena *arena, size_t size);

/* alignment must be a power of two. */
void  *arena_alloc_aligned(MemoryArena *arena, size_t size, size_t alignment);

/* Releases every allocation at once. O(1). */
void   arena_reset(MemoryArena *arena);

ArenaMarker arena_mark(const MemoryArena *arena);
void        arena_rewind(MemoryArena *arena, ArenaMarker marker);

size_t arena_used(const MemoryArena *arena);
size_t arena_remaining(const MemoryArena *arena);
float  arena_usage_ratio(const MemoryArena *arena);

#define ARENA_PUSH_STRUCT(arena, T)    ((T *)arena_alloc((arena), sizeof(T)))
#define ARENA_PUSH_ARRAY(arena, T, n)  ((T *)arena_alloc((arena), sizeof(T) * (size_t)(n)))

#endif /* MEMORY_ARENA_H */
