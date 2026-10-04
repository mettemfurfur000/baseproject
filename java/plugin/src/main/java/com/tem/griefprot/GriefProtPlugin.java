package com.tem.griefprot;

import com.tem.griefprot.config.ConfigLoader;
import com.tem.griefprot.config.PluginConfig;
import com.tem.griefprot.nativebridge.Abi;
import com.tem.griefprot.world.GriefWorld;
import com.tem.griefprot.world.WorldPolicy;
import java.nio.file.Path;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.logging.Level;
import org.bukkit.World;
import org.bukkit.plugin.java.JavaPlugin;

/**
 * Plugin entry point.
 *
 * <p>Startup order matters and is deliberate: the native library is loaded and
 * its ABI checked before any world exists, because a mismatch found after the
 * first world is created is far harder to diagnose than one found immediately.
 */
public final class GriefProtPlugin extends JavaPlugin {

    /** Native major version this plugin's bindings were written against. */
    private static final int EXPECTED_ABI_MAJOR = 1;

    private Abi abi;
    private Faces faces;
    private PluginConfig config;
    private WorldPolicy policy;
    private final Map<UUID, GriefWorld> worlds = new HashMap<>();

    @Override
    public void onEnable() {
        saveDefaultConfig();
        config = ConfigLoader.load(getConfig());

        try {
            abi = new Abi();
            abi.verify(EXPECTED_ABI_MAJOR, 0);
        } catch (Throwable t) {
            // Without a working, matching library there is nothing to run. Failing
            // here is far kinder than letting every block event fail later.
            getLogger().log(Level.SEVERE, "griefprot could not start", t);
            getServer().getPluginManager().disablePlugin(this);
            return;
        }

        getLogger().info(() -> "native library ok, abi " + (abi.getVersion() >>> 16) + "."
                + (abi.getVersion() & 0xFFFF));

        faces = new Faces(abi);
        HostSink.ensureInitialised();
        policy = WorldPolicy.from(this);

        for (World world : getServer().getWorlds()) {
            addWorld(world);
        }

        getServer().getPluginManager().registerEvents(new ChunkListener(this), this);
        getServer().getPluginManager().registerEvents(new BreakListener(this), this);
        getServer().getPluginManager().registerEvents(new ExplosionListener(this), this);
        getServer().getPluginManager().registerEvents(new InteractListener(this), this);
        getServer().getPluginManager().registerEvents(new PistonListener(this), this);
        DebugOverlay debugOverlay = new DebugOverlay(this);
        getServer().getPluginManager().registerEvents(debugOverlay, this);
        Objects.requireNonNull(getCommand("griefprot"), "plugin.yml must declare the griefprot command")
                .setExecutor(debugOverlay);
        debugOverlay.runTaskTimer(this, 1L, 10L);
        // getServer().getScheduler().runTaskTimer(this, new ShieldTicker(this), 1L, 1L);
        new ShieldTicker(this).runTaskTimerAsynchronously(this, 1L, 1L);

        getLogger().info("enabled for " + worlds.size() + " dimension(s)");
    }

    @Override
    public void onDisable() {
        // Reverse order so the callback hosts go away before the scheduler can
        // fire again.
        for (World world : getServer().getWorlds()) {
            removeWorld(world);
        }
        getLogger().info("disabled");
    }

    /** Brings a dimension under management. Called for worlds present at startup and later. */
    public void addWorld(World world) {
        if (worlds.containsKey(world.getUID())) {
            return;
        }
        Path dataFile = getDataFolder().toPath().resolve("worlds").resolve(world.getName() + ".gpdata");
        try {
            GriefWorld griefWorld = GriefWorld.open(abi, world, policy, config, dataFile, getLogger());
            worlds.put(world.getUID(), griefWorld);
            getLogger().info("tracking " + world.getName() + " at " + dataFile);
        } catch (Exception e) {
            getLogger().log(Level.SEVERE, "could not track " + world.getName(), e);
        }
    }

    /** Saves and releases a dimension. */
    public void removeWorld(World world) {
        GriefWorld griefWorld = worlds.remove(world.getUID());
        if (griefWorld != null) {
            griefWorld.close();
        }
    }

    /** The native binding for a Bukkit world, or {@code null} if it is not tracked. */
    public GriefWorld grief(World world) {
        return worlds.get(world.getUID());
    }

    /** The native binding for a block's world, or {@code null} if not tracked. */
    public GriefWorld grief(org.bukkit.block.Block block) {
        return grief(block.getWorld());
    }

    /** Native binding. */
    public Abi abi() {
        return abi;
    }

    /** Bukkit face translation against the library's ids. */
    public Faces faces() {
        return faces;
    }

    /** Startup configuration. */
    public PluginConfig config() {
        return config;
    }

    /** Every tracked dimension. */
    public Collection<GriefWorld> trackedWorlds() {
        return worlds.values();
    }
}