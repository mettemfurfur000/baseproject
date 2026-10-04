package com.tem.griefprot;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;

/**
 * Remembers which face a player last aimed at.
 *
 * <p>{@code BlockBreakEvent} carries no face. Reinforcement is per face, so
 * without this a player would always break the weakest face no matter where they
 * clicked, and a block reinforced on one side would stop protecting that side.
 *
 * <p>{@code PlayerInteractEvent} with {@code LEFT_CLICK_BLOCK} does report the
 * face, and in practice arrives before the break for the same click. The mapping
 * is therefore cached on the way in and read on the way out.
 *
 * <p>The cache is deliberately a hint, never a fact. A remembered face is used
 * only when it still refers to the very block being broken, on the same click.
 * Anything else yields {@code null} and the caller falls back to the weakest
 * face, which is what an attacker could aim for anyway. Without that check a
 * stale or spoofed entry would promise protection the block does not have.
 */
final class BreakFaces {

    /** Entries per player. A player only has a few targets within reach. */
    private static final int MAX_ENTRIES = 8;

    /**
     * How long a remembered face stays plausible. A left click and the break it
     * causes are the same tick or the next one; anything older is not that click.
     */
    private static final long MAX_AGE_MILLIS = 2_000L;

    private record Entry(UUID world, int x, int y, int z, BlockFace face, long at) {

        boolean describes(Block block) {
            return world.equals(block.getWorld().getUID())
                    && x == block.getX()
                    && y == block.getY()
                    && z == block.getZ();
        }
    }

    private final Map<UUID, LinkedHashMap<String, Entry>> byPlayer = new LinkedHashMap<>();

    /** Records the face a player just aimed at. */
    synchronized void remember(UUID player, Block block, BlockFace face) {
        if (face == null) {
            return;
        }
        LinkedHashMap<String, Entry> entries =
                byPlayer.computeIfAbsent(player, id -> new LinkedHashMap<>());
        entries.put(key(block), new Entry(block.getWorld().getUID(), block.getX(), block.getY(), block.getZ(),
                face, System.currentTimeMillis()));
        while (entries.size() > MAX_ENTRIES) {
            var oldest = entries.entrySet().iterator();
            oldest.next();
            oldest.remove();
        }
    }

    /**
     * The face this player aimed at for exactly this block on the current click,
     * or {@code null} when there is nothing trustworthy to go on.
     */
    synchronized BlockFace recall(UUID player, Block block) {
        LinkedHashMap<String, Entry> entries = byPlayer.get(player);
        if (entries == null) {
            return null;
        }
        Entry entry = entries.get(key(block));
        if (entry == null || !entry.describes(block)) {
            return null;
        }
        if (System.currentTimeMillis() - entry.at() > MAX_AGE_MILLIS) {
            return null;
        }
        return entry.face();
    }

    /** Drops everything remembered for a player, on quit. */
    synchronized void forget(UUID player) {
        byPlayer.remove(player);
    }

    private static String key(Block block) {
        return block.getX() + ":" + block.getY() + ":" + block.getZ();
    }
}