package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.world.ChunkLoadEvent;
import org.bukkit.event.world.ChunkUnloadEvent;
import org.bukkit.event.world.WorldLoadEvent;
import org.bukkit.event.world.WorldUnloadEvent;

/**
 * Keeps the native store's chunk residency in step with the server's.
 *
 * <p>Two rules the store relies on:
 *
 * <ul>
 *   <li>A query never loads a chunk. So a block that is only queried, never
 *       loaded, never enters the store.
 *   <li>An unloaded chunk is released. Anything still pointing into it, shields
 *       especially, has to go first or it will keep paying out against
 *       coordinates the server no longer considers real.
 * </ul>
 *
 * <p>Events run at {@link EventPriority#MONITOR} because nothing here changes
 * what the server does; it only mirrors it. Cancelling is never appropriate.
 */
public final class ChunkListener implements Listener {

    private final GriefProtPlugin plugin;

    public ChunkListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.MONITOR, ignoreCancelled = false)
    public void onWorldLoad(WorldLoadEvent event) {
        plugin.addWorld(event.getWorld());
    }

    @EventHandler(priority = EventPriority.MONITOR)
    public void onWorldUnload(WorldUnloadEvent event) {
        plugin.removeWorld(event.getWorld());
    }

    @EventHandler(priority = EventPriority.MONITOR)
    public void onChunkLoad(ChunkLoadEvent event) {
        GriefWorld world = plugin.grief(event.getWorld());
        if (world != null) {
            world.onChunkLoad(event.getChunk());
        }
    }

    @EventHandler(priority = EventPriority.MONITOR)
    public void onChunkUnload(ChunkUnloadEvent event) {
        GriefWorld world = plugin.grief(event.getWorld());
        if (world != null) {
            world.onChunkUnload(event.getChunk());
        }
    }
}