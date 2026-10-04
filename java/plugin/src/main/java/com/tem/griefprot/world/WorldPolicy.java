package com.tem.griefprot.world;

import com.tem.griefprot.GriefProtPlugin;

/**
 * The host's answers to the questions the C library cannot answer for itself.
 *
 * <p>Everything Minecraft-specific lives behind this interface, which keeps the
 * native bridge testable without a running server and keeps {@link GriefWorld}
 * free of Bukkit lookups.
 */
public interface WorldPolicy {

    /**
     * May this position carry protection at all?
     *
     * <p>The library calls this before it allocates anything, so a "no" is also
     * the cheapest way to keep arbitrary blocks out of the store. Air and liquids
     * must return false.
     */
    boolean protectable(GriefWorld world, int x, int y, int z);

    /**
     * Hands out shield points for a block a live shield is covering.
     *
     * @param amount how many points the shield is offering
     * @param cap the most the block may hold
     * @return how many were actually granted; 0 is always a valid answer
     */
    int grant(GriefWorld world, int x, int y, int z, int amount, int cap);

    /**
     * A shield is covering this block at {@code now}. The library already
     * recorded the cover itself; this is the hook for a host that wants to react,
     * such as emitting particles.
     */
    void cover(GriefWorld world, int x, int y, int z, long now);

    /** The policy configured for this plugin instance. */
    static WorldPolicy from(GriefProtPlugin plugin) {
        return new BukkitWorldPolicy(plugin);
    }
}