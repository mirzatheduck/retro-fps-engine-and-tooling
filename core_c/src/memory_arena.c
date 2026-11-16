/*
 * memory_arena.c - Fixed-size linear arena allocator implementation.
 */
#include "memory_arena.h"

#include <stdint.h>
#include <string.h>

static int is_power_of_two(size_t x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

void arena_init(MemoryArena *arena, void *backing, size_t capacity)
{
    arena->base = (unsigned char *)backing;
    arena->capacity = backing ? capacity : 0;
    arena->offset = 0;
    arena->peak = 0;
    arena->alloc_count = 0;
    arena->failed_allocs = 0;
}

void *arena_alloc_aligned(MemoryArena *arena, size_t size, size_t alignment)
{
    uintptr_t current, aligned;
    size_t padding, new_offset;

    if (arena == NULL || arena->base == NULL || size == 0 || !is_power_of_two(alignment)) {
        if (arena) arena->failed_allocs++;
        return NULL;
    }

    current = (uintptr_t)(arena->base + arena->offset);
    aligned = (current + (uintptr_t)(alignment - 1)) & ~(uintptr_t)(alignment - 1);
    padding = (size_t)(aligned - current);

    /* overflow-safe capacity check */
    if (padding > arena->capacity - arena->offset ||
        size > arena->capacity - arena->offset - padding) {
        arena->failed_allocs++;
        return NULL;
    }

    new_offset = arena->offset + padding + size;
    arena->offset = new_offset;
    arena->alloc_count++;
    if (new_offset > arena->peak) arena->peak = new_offset;

    memset((void *)aligned, 0, size);
    return (void *)aligned;
}

void *arena_alloc(MemoryArena *arena, size_t size)
{
    return arena_alloc_aligned(arena, size, ARENA_DEFAULT_ALIGNMENT);
}

void arena_reset(MemoryArena *arena)
{
    arena->offset = 0;
    arena->alloc_count = 0;
}

ArenaMarker arena_mark(const MemoryArena *arena)
{
    ArenaMarker m;
    m.offset = arena->offset;
    m.alloc_count = arena->alloc_count;
    return m;
}

void arena_rewind(MemoryArena *arena, ArenaMarker marker)
{
    if (marker.offset <= arena->offset) {
        arena->offset = marker.offset;
        arena->alloc_count = marker.alloc_count;
    }
}

size_t arena_used(const MemoryArena *arena)
{
    return arena->offset;
}

size_t arena_remaining(const MemoryArena *arena)
{
    return arena->capacity - arena->offset;
}

float arena_usage_ratio(const MemoryArena *arena)
{
    return arena->capacity ? (float)arena->offset / (float)arena->capacity : 0.0f;
}
