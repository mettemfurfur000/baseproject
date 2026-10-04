package com.tem.griefprot.nativebridge;

/** A block position, kept as a record only so it never has to cross into C. */
public record Pos(int x, int y, int z) {}