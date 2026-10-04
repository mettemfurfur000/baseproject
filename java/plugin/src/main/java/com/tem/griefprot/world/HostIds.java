package com.tem.griefprot.world;

import java.util.concurrent.atomic.AtomicLong;

/**
 * Hands out the ids the native library passes back to host callbacks.
 *
 * <p>Zero is reserved so an uninitialised or cleared id never resolves to a
 * real world. Ids are never reused, because a callback in flight must not find
 * a different dimension after the original world was closed.
 */
final class HostIds {

    private static final AtomicLong NEXT = new AtomicLong(1);

    private HostIds() {}

    static long next() {
        return NEXT.getAndIncrement();
    }
}