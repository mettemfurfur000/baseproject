package com.tem.griefprot;

import com.tem.griefprot.nativebridge.Abi;
import com.tem.griefprot.nativebridge.BreakResult;
import com.tem.griefprot.world.GriefWorld;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.bukkit.FluidCollisionMode;
import org.bukkit.Location;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.block.BlockExplodeEvent;
import org.bukkit.event.entity.EntityExplodeEvent;
import org.bukkit.util.RayTraceResult;
import org.bukkit.util.Vector;

/** Resolves explosion damage against the same protection store as player breaks. */
public final class ExplosionListener implements Listener {

    private final GriefProtPlugin plugin;

    public ExplosionListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onEntityExplode(EntityExplodeEvent event) {
        protectAffectedBlocks(event.getLocation(), event.blockList());
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onBlockExplode(BlockExplodeEvent event) {
        Location origin = event.getBlock().getLocation().add(0.5, 0.5, 0.5);
        protectAffectedBlocks(origin, event.blockList());
    }

    private void protectAffectedBlocks(Location origin, List<Block> affectedBlocks) {
        GriefWorld world = plugin.grief(origin.getWorld());
        if (world == null) {
            return;
        }

        Set<BlockPosition> heldBlocks = new HashSet<>();
        Set<BlockPosition> occludedBlocks = new HashSet<>();
        List<Block> candidates = new ArrayList<>(affectedBlocks);
        candidates.sort(Comparator.comparingDouble(block -> distanceSquared(origin, block)));
        for (Block block : candidates) {
            BlockPosition position = BlockPosition.of(block);
            RayTraceResult trace = traceToBlock(origin, block);
            if (isBehindProtectedBlock(position, trace, heldBlocks)) {
                occludedBlocks.add(position);
                continue;
            }
            if (!world.hasProtection(block.getX(), block.getY(), block.getZ())) {
                continue;
            }

            BlockFace struckFace = trace != null
                            && block.equals(trace.getHitBlock())
                            && trace.getHitBlockFace() != null
                    ? trace.getHitBlockFace()
                    : faceToward(
                            origin.getX(), origin.getY(), origin.getZ(), block.getX(), block.getY(), block.getZ());
            int face = plugin.faces().toAbi(struckFace);
            if (face == Abi.NO_FACE) {
                face = world.abi().weakestFace(
                        world.handle(), block.getX(), block.getY(), block.getZ());
                if (face == Abi.NO_FACE) {
                    face = plugin.abi().faceNone();
                }
            }

            BreakResult result = world.resolveBreak(block, face, true, true);
            if (!result.ok()) {
                plugin.getLogger().warning("could not resolve explosion protection at "
                        + block.getWorld().getName() + " " + block.getX() + "," + block.getY() + ","
                        + block.getZ() + ": " + world.abi().lastError());
                continue;
            }
            if (result.protectedFromBreak()) {
                heldBlocks.add(position);
            }
        }

        for (Iterator<Block> blocks = affectedBlocks.iterator(); blocks.hasNext();) {
            BlockPosition position = BlockPosition.of(blocks.next());
            if (heldBlocks.contains(position) || occludedBlocks.contains(position)) {
                blocks.remove();
            }
        }
    }

    private static double distanceSquared(Location origin, Block block) {
        double dx = block.getX() + 0.5 - origin.getX();
        double dy = block.getY() + 0.5 - origin.getY();
        double dz = block.getZ() + 0.5 - origin.getZ();
        return dx * dx + dy * dy + dz * dz;
    }

    private static RayTraceResult traceToBlock(Location origin, Block target) {
        double dx = target.getX() + 0.5 - origin.getX();
        double dy = target.getY() + 0.5 - origin.getY();
        double dz = target.getZ() + 0.5 - origin.getZ();
        double distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
        if (distance == 0.0) {
            return null;
        }

        Vector direction = new Vector(dx / distance, dy / distance, dz / distance);
        double offset = Math.min(distance, distanceToCellExit(origin, direction) + 1.0e-4);
        if (offset >= distance) {
            return null;
        }
        Location traceOrigin = origin.clone().add(direction.clone().multiply(offset));
        return origin.getWorld().rayTraceBlocks(
                traceOrigin, direction, distance - offset, FluidCollisionMode.NEVER, true);
    }

    private static boolean isBehindProtectedBlock(
            BlockPosition target, RayTraceResult trace, Set<BlockPosition> heldBlocks) {
        if (trace == null || trace.getHitBlock() == null) {
            return false;
        }
        return isProtectedObstruction(target, BlockPosition.of(trace.getHitBlock()), heldBlocks);
    }

    private static double distanceToCellExit(Location origin, Vector direction) {
        double x = distanceToCellExit(origin.getX(), direction.getX());
        double y = distanceToCellExit(origin.getY(), direction.getY());
        double z = distanceToCellExit(origin.getZ(), direction.getZ());
        return Math.min(x, Math.min(y, z));
    }

    private static double distanceToCellExit(double coordinate, double direction) {
        if (direction > 0.0) {
            return (Math.floor(coordinate) + 1.0 - coordinate) / direction;
        }
        if (direction < 0.0) {
            return (Math.floor(coordinate) - coordinate) / direction;
        }
        return Double.POSITIVE_INFINITY;
    }

    static boolean isProtectedObstruction(
            BlockPosition target, BlockPosition rayHit, Set<BlockPosition> heldBlocks) {
        return !target.equals(rayHit) && heldBlocks.contains(rayHit);
    }

    /**
     * Returns the block face nearest the explosion origin, or SELF when the
     * origin is exactly at the block center.
     */
    static BlockFace faceToward(double originX, double originY, double originZ, int blockX, int blockY, int blockZ) {
        double dx = originX - (blockX + 0.5);
        double dy = originY - (blockY + 0.5);
        double dz = originZ - (blockZ + 0.5);
        double ax = Math.abs(dx);
        double ay = Math.abs(dy);
        double az = Math.abs(dz);

        if (ax == 0.0 && ay == 0.0 && az == 0.0) {
            return BlockFace.SELF;
        }
        if (ax >= ay && ax >= az) {
            return dx > 0 ? BlockFace.EAST : BlockFace.WEST;
        }
        if (ay >= az) {
            return dy > 0 ? BlockFace.UP : BlockFace.DOWN;
        }
        return dz > 0 ? BlockFace.SOUTH : BlockFace.NORTH;
    }

    record BlockPosition(UUID world, int x, int y, int z) {
        static BlockPosition of(Block block) {
            return new BlockPosition(block.getWorld().getUID(), block.getX(), block.getY(), block.getZ());
        }
    }
}
