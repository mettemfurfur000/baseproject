package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import java.time.Duration;
import org.bukkit.Chunk;
import org.bukkit.scheduler.BukkitRunnable;
import org.bukkit.util.Vector;

/**
 * Advances shields and sweeps away decayed protection.
 *
 * <p>The library only compares differences between timestamps, so any monotonic
 * clock works. {@link System#nanoTime()} is used rather than
 * {@code currentTimeMillis()} because it is immune to the wall clock jumping
 * backwards, which would otherwise hand a shield a huge negative interval and
 * either drain it instantly or skip its whole window.
 *
 * <p>Decay runs on a timer rather than every tick. Sweeping every loaded chunk
 * every tick would be pure overhead, since the shortest interval a record can
 * have is measured in seconds.
 */
public final class ShieldTicker extends BukkitRunnable {

    /** How often the decay sweep runs. */
    private static final long DECAY_PERIOD_TICKS = 100L;

    private final GriefProtPlugin plugin;
    private final long startNanos = System.nanoTime();

    private long nextDecayNanos;

    public ShieldTicker(GriefProtPlugin plugin) {
        this.plugin = plugin;
        this.nextDecayNanos = startNanos + Duration.ofSeconds(5).toNanos();
    }

    @Override
    public void run() {
        long now = System.nanoTime();
        double seconds = (now - startNanos) / 1_000_000_000.0;

        for (GriefWorld world : plugin.trackedWorlds()) {
            tickShields(world, (long) seconds);
        }

        if (now >= nextDecayNanos) {
            for (GriefWorld world : plugin.trackedWorlds()) {
                sweepDecay(world, (long) seconds);
            }
            nextDecayNanos = now + Duration.ofSeconds(5).toNanos();
        }
    }

    /**
     * Ticks every shield in one dimension.
     *
     * <p>Iterating by index and re-reading the count is deliberate: a shield may
     * be destroyed while it ticks, which would leave a stale handle in the mirror
     * the world keeps, so the list is rebuilt from the store each time.
     */
    private void tickShields(GriefWorld world, long seconds) {
        int count = world.abi().shieldCount(world.handle());
        for (int index = 0; index < count; index++) {
            int shield = world.abi().shieldAt(world.handle(), index);
            if (shield == 0) {
                continue;
            }
            world.abi().shieldTick(shield, seconds);
        }
    }

    private void sweepDecay(GriefWorld world, long seconds) {
        for (Chunk chunk : world.bukkit().getLoadedChunks()) {
            world.decay(chunk, seconds, plugin.config());
        }
    }

    /** Vector helper for callers that want to show a shield's reach. */
    public static Vector reach(int range) {
        return new Vector(range, range, range);
    }
}