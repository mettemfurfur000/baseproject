package com.tem.griefprot.nativebridge;

/**
 * Chunk residency for one world.
 *
 * @param loaded chunks currently resident
 * @param reserved slots the world has reserved in total, which never shrinks
 */
public record ChunkCounts(int loaded, int reserved) {}