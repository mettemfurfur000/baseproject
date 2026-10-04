package com.tem.griefprot.nativebridge;

/**
 * Outcome of one decay sweep over a chunk.
 *
 * @param pointsRemoved shield points that expired
 * @param touched records the sweep looked at
 * @param skipped records it deliberately left alone, for example because a live
 *     shield is still holding them up
 */
public record DecayResult(long pointsRemoved, int touched, int skipped) {}