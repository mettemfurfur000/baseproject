package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import java.util.ArrayList;
import java.util.List;
import org.bukkit.Location;
import org.bukkit.block.Block;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.block.BlockPistonExtendEvent;
import org.bukkit.event.block.BlockPistonRetractEvent;

/**
 * Moves protection along with the blocks a piston pushes.
 *
 * <p>Piston motion is expressed as a direction, not as a set of moved blocks, and
 * the library expects exactly that: {@code gp_abi_move_block} takes the direction
 * a block travels and works out the source and destination itself.
 *
 * <p>The event reports the whole move, but the library moves one block's
 * protection at a time, so each moved block is passed individually. If the store
 * refuses a move, for example because the merge would exceed the face cap, that
 * block's protection simply stays behind: nothing is cancelled, because a piston
 * that cannot carry protection is still a piston.
 */
public final class PistonListener implements Listener {

    private final GriefProtPlugin plugin;

    public PistonListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.MONITOR, ignoreCancelled = true)
    public void onExtend(BlockPistonExtendEvent event) {
        handle(event.getBlock(), event.getDirection(), event.getBlocks());
    }

    @EventHandler(priority = EventPriority.MONITOR, ignoreCancelled = true)
    public void onRetract(BlockPistonRetractEvent event) {
        // A retracting block travels against the piston head's facing.
        handle(event.getBlock(), event.getDirection().getOppositeFace(), event.getBlocks());
    }

    private void handle(Block piston, org.bukkit.block.BlockFace direction, List<Block> blocks) {
        GriefWorld world = plugin.grief(piston);
        if (world == null) {
            return;
        }
        int motion = plugin.faces().toAbi(direction);
        if (motion == com.tem.griefprot.nativebridge.Abi.NO_FACE) {
            return;
        }

        for (Block block : snapshot(blocks)) {
            if (!world.hasProtection(block.getX(), block.getY(), block.getZ())) {
                continue;
            }
            Location to = block.getRelative(direction).getLocation();
            world.abi().moveBlock(
                    world.handle(),
                    block.getX(), block.getY(), block.getZ(),
                    to.getBlockX(), to.getBlockY(), to.getBlockZ(),
                    motion);
        }
    }

    /**
     * Copies the moved blocks.
     *
     * <p>Iterating the live list while the piston is still being applied is
     * fragile, and the snapshot also guards against a block being visited twice
     * when the library moves protection and the event lists both ends of the move.
     */
    private static List<Block> snapshot(List<Block> blocks) {
        return new ArrayList<>(blocks);
    }
}