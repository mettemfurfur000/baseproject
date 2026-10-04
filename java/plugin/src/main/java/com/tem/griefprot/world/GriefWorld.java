package com.tem.griefprot.world;

import com.tem.griefprot.HostSink;
import com.tem.griefprot.config.PluginConfig;
import com.tem.griefprot.nativebridge.Abi;
import com.tem.griefprot.nativebridge.BreakResult;
import com.tem.griefprot.nativebridge.ChunkCounts;
import com.tem.griefprot.nativebridge.DecayResult;
import com.tem.griefprot.nativebridge.Pos;
import com.tem.griefprot.nativebridge.WorldConfig;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.logging.Level;
import java.util.logging.Logger;
import org.bukkit.Chunk;
import org.bukkit.Location;
import org.bukkit.World;
import org.bukkit.block.Block;

/**
 * One native world bound to one Bukkit dimension.
 *
 * <p>Each dimension gets its own {@code gp_h_world} rather than sharing one,
 * because the library's world is the unit of persistence and of chunk residency.
 * Two dimensions sharing it would let a player reinforce a block in the Nether
 * that pays out in the Overworld.
 *
 * <p>Handles are recycled slot indexes with no generation tag, so this object
 * must not outlive the native world it wraps. {@link #close()} unregisters the
 * callback host id before destroying the world, which is what stops a stale id
 * from resolving to a different dimension later.
 */
public final class GriefWorld implements AutoCloseable {

    private final Abi abi;
    private final World bukkit;
    private final WorldPolicy policy;
    private final Path dataFile;
    private final Logger log;

    private final int handle;
    private final long hostId;

    /** Shield handles this world owns, kept so chunk unload can retire them. */
    private final List<Integer> shields = new ArrayList<>();

    private boolean closed;

    private GriefWorld(Abi abi, World bukkit, WorldPolicy policy, Path dataFile, Logger log, long hostId) {
        this.abi = abi;
        this.bukkit = bukkit;
        this.policy = policy;
        this.dataFile = dataFile;
        this.log = log;
        this.hostId = hostId;
        this.handle = abi.worldCreate();
    }

    /**
     * Creates a native world for a dimension, applies configuration, installs
     * the callbacks and restores any saved data.
     *
     * @throws IllegalStateException if the native world could not be created
     */
    public static GriefWorld open(
            Abi abi, World bukkit, WorldPolicy policy, PluginConfig config, Path dataFile, Logger log)
            throws IOException {
        Objects.requireNonNull(bukkit, "bukkit");

        long hostId = HostIds.next();
        GriefWorld world = new GriefWorld(abi, bukkit, policy, dataFile, log, hostId);

        if (world.handle == 0) {
            throw new IllegalStateException(
                    "the native library refused to create a world for " + bukkit.getName() + ": " + abi.lastError());
        }

        world.applyConfig(config);
        HostSink.register(hostId, world);
        world.installSink();
        world.load();
        return world;
    }

    private void applyConfig(PluginConfig config) {
        // Only chunk height is derived from Bukkit; everything else is the
        // plugin's own tuning so the numbers stay in one place.
        int height = bukkit.getMaxHeight() - bukkit.getMinHeight();

        abi.worldConfig(
                handle,
                config.maxFacesPerBlock(),
                config.shieldPool(),
                config.shieldRegen(),
                config.shieldWindow(),
                config.shieldRange(),
                config.shieldMaxTargets(),
                config.shieldFuelMax(),
                config.shieldFuelPerSec(),
                config.decayInterval(),
                config.decayAmount(),
                height,
                config.copperBreaks(),
                config.ironBreaks());

        WorldConfig applied = abi.worldConfigGet(handle);
        if (applied.maxFacesPerBlock() != config.maxFacesPerBlock()) {
            // Not fatal, but it means the requested value was not what took
            // effect, and every caller reasoning about face slots would be wrong.
            throw new IllegalStateException(
                    "shield library clamped maxFacesPerBlock to " + applied.maxFacesPerBlock()
                            + " but " + config.maxFacesPerBlock() + " was requested");
        }
        if (applied.chunkHeight() != height) {
            throw new IllegalStateException(
                    "shield library clamped chunkHeight to " + applied.chunkHeight()
                            + " but the dimension " + bukkit.getName() + " is " + height + " blocks tall");
        }
    }

    private void installSink() {
        boolean ok = abi.sinkConfigure(
                handle, hostId, HostSink.protectableStub(), HostSink.grantStub(), HostSink.coverStub());
        if (!ok) {
            throw new IllegalStateException("could not install host callbacks: " + abi.lastError());
        }
    }

    // --- identity -------------------------------------------------------------

    public Abi abi() {
        return abi;
    }

    public World bukkit() {
        return bukkit;
    }

    public WorldPolicy policy() {
        return policy;
    }

    /** The native world handle. Only valid while this object is open. */
    public int handle() {
        return handle;
    }

    public long hostId() {
        return hostId;
    }

    // --- chunk lifecycle ------------------------------------------------------

    /**
     * Brings a chunk into the native store.
     *
     * <p>Called on chunk load. Failures are logged and swallowed: a protection
     * store that cannot be grown should not stop the chunk from existing.
     */
    public void onChunkLoad(Chunk chunk) {
        if (closed) {
            return;
        }
        abi.chunkAt(handle, chunk.getX() * 16, chunk.getWorld().getMinHeight(), chunk.getZ() * 16);
    }

    /**
     * Releases a chunk from the native store.
     *
     * <p>Any shield sitting in this chunk goes with it. A shield handle that
     * outlived its chunk would keep ticking and paying out against coordinates
     * the server no longer considers loaded.
     */
    public void onChunkUnload(Chunk chunk) {
        if (closed) {
            return;
        }
        retireShieldsIn(chunk);
        if (!abi.chunkUnload(handle, chunk.getX(), chunk.getZ())) {
            // Nothing resident there. Benign: unload can race our own bookkeeping.
            return;
        }
    }

    private void retireShieldsIn(Chunk chunk) {
        int count = abi.shieldCount(handle);
        for (int index = count - 1; index >= 0; index--) {
            int shield = abi.shieldAt(handle, index);
            if (shield == 0) {
                continue;
            }
            Pos pos = abi.shieldGetPos(shield);
            // Shifting the coordinate gives the chunk without a division, and
            // stays correct for negative coordinates, which matter in the Nether
            // where >> 4 on a negative int still floors but / 16 truncates.
            if ((pos.x() >> 4) == chunk.getX() && (pos.z() >> 4) == chunk.getZ()) {
                abi.shieldDestroy(shield);
                shields.remove(Integer.valueOf(shield));
            }
        }
    }

    // --- gameplay -------------------------------------------------------------

    /** Resolves a break against the native store. */
    public BreakResult resolveBreak(Block block, int face, boolean useReinf, boolean useShield) {
        return abi.resolveBreak(handle, block.getX(), block.getY(), block.getZ(), face, useReinf, useShield);
    }

    /** Whether any protection at all is recorded for a position. */
    public boolean hasProtection(int x, int y, int z) {
        return abi.reinfFaceCount(handle, x, y, z) > 0 || abi.pointsGet(handle, x, y, z) > 0;
    }

    /** Registers a shield created through this world, so unload can retire it. */
    public void trackShield(int shield) {
        shields.add(shield);
    }

    /** Every shield handle currently alive in this world. */
    public List<Integer> shields() {
        return List.copyOf(shields);
    }

    /** Runs one decay sweep over a chunk. */
    public DecayResult decay(Chunk chunk, long now, PluginConfig config) {
        int baseY = bukkit.getMinHeight();
        return abi.chunkDecay(
                handle, chunk.getX() * 16, baseY, chunk.getZ() * 16, now, config.decayInterval(), config.decayAmount());
    }

    /** How much protection data this dimension holds. */
    public long memoryBytes() {
        return abi.worldMemory(handle);
    }

    public ChunkCounts chunkCounts() {
        return abi.chunkCounts(handle);
    }

    /** Location for a native position. */
    public Location location(int x, int y, int z) {
        return new Location(bukkit, x, y, z);
    }

    // --- persistence ----------------------------------------------------------

    /** Writes this dimension's state. Returns false if the library reported an error. */
    public boolean save() {
        if (closed) {
            return false;
        }
        try {
            Files.createDirectories(dataFile.getParent());
            if (!abi.worldSave(handle, dataFile.toString())) {
                log.log(Level.SEVERE, "could not save griefprot data for " + bukkit.getName()
                        + ": " + abi.lastError());
                return false;
            }
            return true;
        } catch (IOException e) {
            log.log(Level.SEVERE, "could not write " + dataFile, e);
            return false;
        }
    }

    private void load() throws IOException {
        if (!Files.exists(dataFile)) {
            return;
        }
        // Restore into the existing handle. The library releases any shields
        // already attached, so this must happen before anything is tracked.
        int restored = abi.worldLoad(handle, dataFile.toString(), abi.worldConfigGet(handle).shieldPool(),
                abi.worldConfigGet(handle).shieldMaxTargets());
        if (restored < 0) {
            throw new IOException(
                    "could not load griefprot data for " + bukkit.getName() + " from " + dataFile
                            + ": " + abi.lastError());
        }
        // The store owns the shields again after a load, so rebuild the mirror
        // the unload path consults.
        rebuildShieldIndex();
    }

    private void rebuildShieldIndex() {
        shields.clear();
        int count = abi.shieldCount(handle);
        for (int index = 0; index < count; index++) {
            int shield = abi.shieldAt(handle, index);
            if (shield != 0) {
                shields.add(shield);
            }
        }
    }

    // --- teardown -------------------------------------------------------------

    /**
     * Saves, unregisters and destroys the native world.
     *
     * <p>The order matters: the callback host id is withdrawn before the handle
     * is released, so a callback arriving in between finds no world rather than
     * a different dimension that took the recycled slot.
     */
    @Override
    public void close() {
        if (closed) {
            return;
        }
        save();
        HostSink.unregister(hostId);
        abi.worldDestroy(handle);
        closed = true;
        shields.clear();
    }
}