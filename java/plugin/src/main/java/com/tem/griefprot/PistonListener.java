package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import java.util.ArrayList;
import java.util.List;
import org.bukkit.Location;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.block.BlockPistonEvent;
import org.bukkit.event.block.BlockPistonExtendEvent;
import org.bukkit.event.block.BlockPistonRetractEvent;

/**
 * Moves protection along with the blocks a piston pushes or pulls.
 *
 * <p>Piston motion is expressed as a direction, not as a set of moved blocks, and
 * the library expects exactly that: {@code gp_abi_move_block} takes the direction
 * a block travels and works out the source and destination itself.
 *
 * <p>The event reports the whole move, but the library moves one block's
 * protection at a time, so each moved block is passed individually. If the store
 * refuses any move, for example because the merge would exceed the face cap, the
 * piston event is cancelled and earlier protection transfers are rolled back.
 * Source-face checks differ by event: retracts guard the travel-facing side,
 * while extensions guard the opposite side.
 */
public final class PistonListener implements Listener {

    private final GriefProtPlugin plugin;

    public PistonListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onExtend(BlockPistonExtendEvent event) {
        if (!handle(event, event.getDirection(), event.getBlocks())) {
            event.setCancelled(true);
        }
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onRetract(BlockPistonRetractEvent event) {
        // Paper reports the pulled block's travel direction, not the piston facing.
        if (!handle(event, event.getDirection(), event.getBlocks())) {
            event.setCancelled(true);
        }
    }

    private boolean handle(
            BlockPistonEvent event, BlockFace direction, List<Block> blocks) {
        GriefWorld world = plugin.grief(event.getBlock());
        if (world == null) {
            return true;
        }
        int motion = plugin.faces().toAbi(direction);
        if (motion == com.tem.griefprot.nativebridge.Abi.NO_FACE) {
            return true;
        }

        BlockFace sourceFace = sourceFaceToGuard(
                direction, event instanceof BlockPistonRetractEvent);
        BlockFace enteredFace = direction.getOppositeFace();
        for (Block block : snapshot(blocks)) {
            int sourceDurability = world.abi().reinfFaceDurability(
                    world.handle(), block.getX(), block.getY(), block.getZ(),
                    plugin.faces().toAbi(sourceFace));
            if (sourceDurability > 0) {
                plugin.getLogger().warning("cancelling piston event at "
                        + event.getBlock().getWorld().getName() + " "
                        + event.getBlock().getX() + "," + event.getBlock().getY() + ","
                        + event.getBlock().getZ() + ": block "
                        + block.getX() + "," + block.getY() + "," + block.getZ()
                        + " would move away from reinforced " + sourceFace
                        + " face (durability=" + sourceDurability + ")");
                return false;
            }

            Block destination = block.getRelative(direction);
            int durability = world.abi().reinfFaceDurability(
                    world.handle(),
                    destination.getX(), destination.getY(), destination.getZ(),
                    plugin.faces().toAbi(enteredFace));
            if (durability > 0) {
                plugin.getLogger().warning("cancelling piston event at "
                        + event.getBlock().getWorld().getName() + " "
                        + event.getBlock().getX() + "," + event.getBlock().getY() + ","
                        + event.getBlock().getZ() + ": block "
                        + block.getX() + "," + block.getY() + "," + block.getZ()
                        + " would move into reinforced " + enteredFace + " face at "
                        + destination.getX() + "," + destination.getY() + ","
                        + destination.getZ() + " (durability=" + durability + ")");
                return false;
            }
        }

        List<Move> completed = new ArrayList<>();
        for (Block block : snapshot(blocks)) {
            if (!world.hasProtection(block.getX(), block.getY(), block.getZ())) {
                continue;
            }
            Location to = block.getRelative(direction).getLocation();
            if (!world.abi().moveBlock(
                    world.handle(),
                    block.getX(), block.getY(), block.getZ(),
                    to.getBlockX(), to.getBlockY(), to.getBlockZ(),
                    motion)) {
                plugin.getLogger().warning("piston protection move rejected; cancelling event at "
                        + event.getBlock().getWorld().getName() + " "
                        + event.getBlock().getX() + "," + event.getBlock().getY() + ","
                        + event.getBlock().getZ() + " for block "
                        + block.getX() + "," + block.getY() + "," + block.getZ()
                        + ": " + world.abi().lastError());
                rollback(world, completed, direction.getOppositeFace());
                return false;
            }
            completed.add(new Move(
                    block.getX(), block.getY(), block.getZ(),
                    to.getBlockX(), to.getBlockY(), to.getBlockZ()));
        }
        return true;
    }

    static BlockFace sourceFaceToGuard(BlockFace travelDirection, boolean retract) {
        return retract ? travelDirection : travelDirection.getOppositeFace();
    }

    private void rollback(GriefWorld world, List<Move> completed, BlockFace reverseDirection) {
        int reverseMotion = plugin.faces().toAbi(reverseDirection);
        for (int i = completed.size() - 1; i >= 0; i--) {
            Move move = completed.get(i);
            if (!world.abi().moveBlock(
                    world.handle(),
                    move.toX(), move.toY(), move.toZ(),
                    move.fromX(), move.fromY(), move.fromZ(),
                    reverseMotion)) {
                plugin.getLogger().severe("failed to roll back piston protection move from "
                        + move.toX() + "," + move.toY() + "," + move.toZ() + " to "
                        + move.fromX() + "," + move.fromY() + "," + move.fromZ()
                        + ": " + world.abi().lastError());
            }
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

    private record Move(int fromX, int fromY, int fromZ, int toX, int toY, int toZ) {}
}