package com.tem.griefprot;

import com.tem.griefprot.nativebridge.Abi;
import com.tem.griefprot.nativebridge.BreakResult;
import com.tem.griefprot.world.GriefWorld;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
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

    private static final AtomicLong EXPLOSION_IDS = new AtomicLong();
    private static final List<BlockFace> FACES =
            List.of(BlockFace.DOWN, BlockFace.UP, BlockFace.NORTH, BlockFace.SOUTH, BlockFace.WEST, BlockFace.EAST);

    private final GriefProtPlugin plugin;

    public ExplosionListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onEntityExplode(EntityExplodeEvent event) {
        protectAffectedBlocks(
                event.getLocation(), event.blockList(), "entity:" + event.getEntity().getType());
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onBlockExplode(BlockExplodeEvent event) {
        Location origin = event.getBlock().getLocation().add(0.5, 0.5, 0.5);
        protectAffectedBlocks(origin, event.blockList(), "block:" + event.getBlock().getType());
    }

    private void protectAffectedBlocks(Location origin, List<Block> affectedBlocks, String source) {
        GriefWorld world = plugin.grief(origin.getWorld());
        if (world == null) {
            return;
        }

        long explosionId = EXPLOSION_IDS.incrementAndGet();
        Set<BlockPosition> cancelledBlocks = new HashSet<>();
        Set<FacePosition> consumedFaces = new HashSet<>();
        Map<BlockPosition, BlockAudit> audits = new LinkedHashMap<>();
        List<Block> candidates = new ArrayList<>(affectedBlocks);
        candidates.sort(Comparator.comparingDouble(block -> distanceSquared(origin, block)));
        for (Block block : candidates) {
            BlockPosition position = BlockPosition.of(block);
            BlockAudit audit = auditFor(audits, world, block);
            RayPath path = traceToBlock(origin, block, world, audits, consumedFaces);
            audit.addRay("target=" + position(block) + " " + path.trace());
            String decision;
            if (path.protectedFaceAbsorbed()) {
                cancelledBlocks.add(position);
                decision = "cancelled_by_intervening_reinforced_face";
                audit.setDecision(decision);
                plugin.getLogger().info(auditPrefix(explosionId) + " source=" + source + " origin="
                        + location(origin) + " target=" + position(block) + " decision=" + decision
                        + " ray=" + path.trace());
                continue;
            }
            if (!world.hasProtection(block.getX(), block.getY(), block.getZ())) {
                decision = "unprotected_target_allowed";
                audit.setDecision(decision);
                plugin.getLogger().info(auditPrefix(explosionId) + " source=" + source + " origin="
                        + location(origin) + " target=" + position(block) + " decision=" + decision
                        + " ray=" + path.trace());
                continue;
            }

            RayTraceResult trace = path.targetHit();
            BlockFace actualHitFace = trace != null
                            && block.equals(trace.getHitBlock())
                            && trace.getHitBlockFace() != null
                    ? trace.getHitBlockFace()
                    : faceToward(
                            origin.getX(), origin.getY(), origin.getZ(), block.getX(), block.getY(), block.getZ());
            int face = plugin.faces().toAbi(actualHitFace);
            if (face == Abi.NO_FACE) {
                face = world.abi().weakestFace(
                        world.handle(), block.getX(), block.getY(), block.getZ());
                if (face == Abi.NO_FACE) {
                    face = plugin.abi().faceNone();
                }
            }

            int targetFaceDurability = world.abi().reinfFaceDurability(
                    world.handle(), block.getX(), block.getY(), block.getZ(), face);
            if (targetFaceDurability > 0) {
                BlockFace protectedFace = plugin.faces().toBukkit(face);
                FacePosition targetFace = new FacePosition(position, protectedFace);
                if (!consumedFaces.add(targetFace)) {
                    cancelledBlocks.add(position);
                    decision = "target_protected reinforcementHeld=true duplicateFaceHit=true"
                            + " faceDurability=" + targetFaceDurability;
                    audit.addRay("target_face_duplicate@" + protectedFace + " durability=" + targetFaceDurability);
                    audit.setDecision(decision);
                    plugin.getLogger().info(auditPrefix(explosionId) + " source=" + source + " origin="
                            + location(origin) + " target=" + position(block) + " struckFace=" + actualHitFace
                            + " decision=" + decision + " ray=" + path.trace());
                    continue;
                }
            }

            BreakResult result = world.resolveBreak(block, face, true, true);
            if (!result.ok()) {
                decision = "resolver_failed:" + world.abi().lastError();
                plugin.getLogger().warning(auditPrefix(explosionId) + " source=" + source + " target="
                        + position(block) + " face=" + actualHitFace + " decision=" + decision
                        + " ray=" + path.trace());
                audit.setDecision(decision);
                continue;
            }
            if (targetFaceDurability > 0) {
                int after = world.abi().reinfFaceDurability(
                        world.handle(), block.getX(), block.getY(), block.getZ(), face);
                audit.addRay("target_face@" + plugin.faces().toBukkit(face).name() + " before="
                        + targetFaceDurability + " after=" + after + " blockBroke=" + result.destroyed());
            }
            if (result.protectedFromBreak()) {
                cancelledBlocks.add(position);
                decision = "target_protected reinforcementHeld=" + result.reinforcementHeld()
                        + " shieldPaid=" + result.shieldPaid() + " absorbed=" + result.absorbed()
                        + " shieldLeft=" + result.left();
            } else {
                decision = "target_allowed blockBroke=" + result.destroyed()
                        + " reinforcementHeld=" + result.reinforcementHeld()
                        + " shieldPaid=" + result.shieldPaid() + " absorbed=" + result.absorbed()
                        + " shieldLeft=" + result.left();
            }
            audit.setDecision(decision);
            plugin.getLogger().info(auditPrefix(explosionId) + " source=" + source + " origin="
                    + location(origin) + " target=" + position(block) + " struckFace=" + actualHitFace
                    + " decision=" + decision + " ray=" + path.trace());
        }

        for (Iterator<Block> blocks = affectedBlocks.iterator(); blocks.hasNext();) {
            BlockPosition position = BlockPosition.of(blocks.next());
            if (cancelledBlocks.contains(position)) {
                blocks.remove();
            }
        }

        plugin.getServer().getScheduler().runTask(plugin, () -> logExplosionOutcomes(
                world, explosionId, source, audits.values().stream().toList()));
    }

    private void logExplosionOutcomes(GriefWorld world, long explosionId, String source, List<BlockAudit> audits) {
        if (plugin.grief(world.bukkit()) != world) {
            plugin.getLogger().warning(auditPrefix(explosionId) + " post-check skipped; world is no longer tracked");
            return;
        }
        for (BlockAudit audit : audits) {
            Block block = audit.block();
            boolean broken = block.getType().isAir();
            int[] afterFaces = faceDurability(world, block);
            int afterShield = world.abi().pointsGet(
                    world.handle(), block.getX(), block.getY(), block.getZ());
            String outcome = broken
                    ? "BROKEN"
                    : block.getType() == audit.typeBefore()
                            ? "survived:" + block.getType()
                            : "changed:" + audit.typeBefore() + "->" + block.getType();
            String cause = audit.rays().isEmpty() ? "no ray recorded" : String.join(" || ", audit.rays());
            plugin.getLogger().info(auditPrefix(explosionId) + " post source=" + source + " target="
                    + position(block) + " typeBefore=" + audit.typeBefore() + " outcome=" + outcome
                    + " decision=" + audit.decision()
                    + " facesBefore=" + faceSummary(audit.facesBefore())
                    + " facesAfter=" + faceSummary(afterFaces)
                    + " shieldBefore=" + audit.shieldBefore() + " shieldAfter=" + afterShield
                    + " rays=" + cause);
            if (!broken) {
                for (int i = 0; i < FACES.size(); i++) {
                    int before = audit.facesBefore()[i];
                    int after = afterFaces[i];
                    if (after < before && after < 31) {
                        plugin.getLogger().warning(auditPrefix(explosionId) + " durability_below_31 source="
                                + source + " target=" + position(block) + " face=" + FACES.get(i)
                                + " before=" + before + " after=" + after
                                + " cause=" + cause + " decision=" + audit.decision());
                    }
                }
            }
        }
    }

    private int[] faceDurability(GriefWorld world, Block block) {
        int[] durability = new int[FACES.size()];
        for (int i = 0; i < FACES.size(); i++) {
            int face = plugin.faces().toAbi(FACES.get(i));
            durability[i] = world.abi().reinfFaceDurability(
                    world.handle(), block.getX(), block.getY(), block.getZ(), face);
        }
        return durability;
    }

    private BlockAudit auditFor(Map<BlockPosition, BlockAudit> audits, GriefWorld world, Block block) {
        BlockPosition position = BlockPosition.of(block);
        return audits.computeIfAbsent(position, ignored -> new BlockAudit(
                block,
                block.getType(),
                faceDurability(world, block),
                world.abi().pointsGet(world.handle(), block.getX(), block.getY(), block.getZ())));
    }

    private static String faceSummary(int[] durability) {
        List<String> faces = new ArrayList<>(FACES.size());
        for (int i = 0; i < FACES.size(); i++) {
            faces.add(FACES.get(i) + ":" + durability[i]);
        }
        return String.join(",", faces);
    }

    private static String position(Block block) {
        return block.getWorld().getName() + ":" + block.getX() + "," + block.getY() + "," + block.getZ();
    }

    private static String location(Location location) {
        return location.getX() + "," + location.getY() + "," + location.getZ();
    }


    private static String auditPrefix(long id) {
        return "[explosion-audit:" + id + "]";
    }

    private static double distanceSquared(Location origin, Block block) {
        double dx = block.getX() + 0.5 - origin.getX();
        double dy = block.getY() + 0.5 - origin.getY();
        double dz = block.getZ() + 0.5 - origin.getZ();
        return dx * dx + dy * dy + dz * dz;
    }

    private RayPath traceToBlock(
            Location origin,
            Block target,
            GriefWorld world,
            Map<BlockPosition, BlockAudit> audits,
            Set<FacePosition> consumedFaces) {
        double dx = target.getX() + 0.5 - origin.getX();
        double dy = target.getY() + 0.5 - origin.getY();
        double dz = target.getZ() + 0.5 - origin.getZ();
        double distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
        if (distance == 0.0) {
            return new RayPath(false, null, "origin_inside_target");
        }

        Vector direction = new Vector(dx / distance, dy / distance, dz / distance);
        double travelled = 0.0;
        List<String> traceLog = new ArrayList<>();
        for (int step = 0; step < 512 && travelled < distance; step++) {
            Location traceOrigin = origin.clone().add(direction.clone().multiply(travelled));
            RayTraceResult trace = origin.getWorld().rayTraceBlocks(
                    traceOrigin, direction, distance - travelled, FluidCollisionMode.NEVER, true);
            if (trace == null || trace.getHitBlock() == null) {
                traceLog.add("ray_trace_miss@" + location(traceOrigin));
                return new RayPath(false, null, String.join(";", traceLog));
            }

            Block hitBlock = trace.getHitBlock();
            if (hitBlock.equals(target)) {
                traceLog.add("target_hit@" + position(hitBlock) + " face=" + trace.getHitBlockFace());
                return new RayPath(false, trace, String.join(";", traceLog));
            }

            BlockAudit hitAudit = auditFor(audits, world, hitBlock);
            BlockFace hitFace = trace.getHitBlockFace();
            int entryDurability = hitFace == null ? 0 : faceDurability(world, hitBlock, hitFace);
            traceLog.add("entry@" + position(hitBlock) + " face=" + hitFace + " durability=" + entryDurability);
            if (hitFace != null && entryDurability > 0) {
                FaceConsumption consumed = consumeFace(world, hitBlock, hitFace, entryDurability, consumedFaces);
                traceLog.add(consumed.summary());
                hitAudit.addRay("intervening=" + position(hitBlock) + " " + consumed.summary());
                // This explosion ray is spent on the plate even when this hit
                // exhausts it and lets the reinforced block itself break.
                return new RayPath(true, null, String.join(";", traceLog));
            }

            Vector hit = trace.getHitPosition();
            double hitDistance = (hit.getX() - traceOrigin.getX()) * direction.getX()
                    + (hit.getY() - traceOrigin.getY()) * direction.getY()
                    + (hit.getZ() - traceOrigin.getZ()) * direction.getZ();
            double exitDistance = distanceToCellExit(hit, direction, hitBlock);
            if (Double.isFinite(exitDistance)) {
                for (BlockFace exitFace : exitFaces(
                        hit, direction, hitBlock.getX(), hitBlock.getY(), hitBlock.getZ())) {
                    int exitDurability = faceDurability(world, hitBlock, exitFace);
                    traceLog.add("exit@" + position(hitBlock) + " face=" + exitFace
                            + " durability=" + exitDurability);
                    if (exitDurability > 0) {
                        FaceConsumption consumed =
                                consumeFace(world, hitBlock, exitFace, exitDurability, consumedFaces);
                        traceLog.add(consumed.summary());
                        hitAudit.addRay("intervening=" + position(hitBlock) + " " + consumed.summary());
                        // The ray has crossed a reinforced face inside this
                        // block, even though its entry side was unprotected.
                        return new RayPath(true, null, String.join(";", traceLog));
                    }
                }
            }
            double advance = hitDistance + exitDistance + 1.0e-4;
            if (!(advance > 0.0)) {
                plugin.getLogger().warning("explosion ray could not advance past "
                        + hitBlock.getWorld().getName() + " " + hitBlock.getX() + "," + hitBlock.getY() + ","
                        + hitBlock.getZ());
                traceLog.add("advance_failed@" + position(hitBlock));
                return new RayPath(true, null, String.join(";", traceLog));
            }
            travelled += advance;
        }
        if (travelled < distance) {
            plugin.getLogger().warning("explosion ray exceeded its block traversal limit toward "
                    + target.getWorld().getName() + " " + target.getX() + "," + target.getY() + "," + target.getZ());
            traceLog.add("traversal_limit");
            return new RayPath(true, null, String.join(";", traceLog));
        }
        return new RayPath(false, null, String.join(";", traceLog));
    }

    private int faceDurability(GriefWorld world, Block block, BlockFace face) {
        int faceId = plugin.faces().toAbi(face);
        if (faceId == Abi.NO_FACE) {
            return 0;
        }
        return world.abi().reinfFaceDurability(
                world.handle(), block.getX(), block.getY(), block.getZ(), faceId);
    }

    private FaceConsumption consumeFace(
            GriefWorld world, Block block, BlockFace face, int before, Set<FacePosition> consumedFaces) {
        if (!markFaceHit(consumedFaces, BlockPosition.of(block), face)) {
            return new FaceConsumption(block, face, before, before, false, true);
        }
        int faceId = plugin.faces().toAbi(face);
        BreakResult result = world.resolveBreak(block, faceId, true, false);
        if (!result.ok()) {
            plugin.getLogger().warning("could not resolve explosion ray protection at "
                    + block.getWorld().getName() + " " + block.getX() + "," + block.getY() + ","
                    + block.getZ() + " face " + face + ": " + world.abi().lastError());
            return new FaceConsumption(block, face, before, before, false, false);
        }
        int after = faceDurability(world, block, face);
        return new FaceConsumption(block, face, before, after, true, false);
    }

    static boolean markFaceHit(Set<FacePosition> consumedFaces, BlockPosition block, BlockFace face) {
        return consumedFaces.add(new FacePosition(block, face));
    }

    static List<BlockFace> exitFaces(Vector hit, Vector direction, int blockX, int blockY, int blockZ) {
        double dx = direction.getX();
        double dy = direction.getY();
        double dz = direction.getZ();
        double tx = distanceToBoundary(
                hit.getX(), dx, dx > 0.0 ? blockX + 1.0 : blockX);
        double ty = distanceToBoundary(
                hit.getY(), dy, dy > 0.0 ? blockY + 1.0 : blockY);
        double tz = distanceToBoundary(
                hit.getZ(), dz, dz > 0.0 ? blockZ + 1.0 : blockZ);
        double first = Math.min(tx, Math.min(ty, tz));
        double tolerance = Math.max(1.0, Math.abs(first)) * 1.0e-9;
        List<BlockFace> faces = new ArrayList<>(3);
        if (Math.abs(tx - first) <= tolerance) {
            faces.add(dx > 0.0 ? BlockFace.EAST : BlockFace.WEST);
        }
        if (Math.abs(ty - first) <= tolerance) {
            faces.add(dy > 0.0 ? BlockFace.UP : BlockFace.DOWN);
        }
        if (Math.abs(tz - first) <= tolerance) {
            faces.add(dz > 0.0 ? BlockFace.SOUTH : BlockFace.NORTH);
        }
        return faces;
    }

    static double distanceToCellExit(Location origin, Vector direction) {
        double xBoundary = direction.getX() > 0.0
                ? Math.floor(origin.getX()) + 1.0
                : direction.getX() < 0.0 ? Math.floor(origin.getX()) : Double.POSITIVE_INFINITY;
        double yBoundary = direction.getY() > 0.0
                ? Math.floor(origin.getY()) + 1.0
                : direction.getY() < 0.0 ? Math.floor(origin.getY()) : Double.POSITIVE_INFINITY;
        double zBoundary = direction.getZ() > 0.0
                ? Math.floor(origin.getZ()) + 1.0
                : direction.getZ() < 0.0 ? Math.floor(origin.getZ()) : Double.POSITIVE_INFINITY;
        return distanceToCellExit(
                origin.getX(),
                origin.getY(),
                origin.getZ(),
                direction.getX(),
                direction.getY(),
                direction.getZ(),
                xBoundary,
                yBoundary,
                zBoundary);
    }

    private static double distanceToCellExit(Vector point, Vector direction, Block block) {
        return distanceToCellExit(
                point.getX(),
                point.getY(),
                point.getZ(),
                direction.getX(),
                direction.getY(),
                direction.getZ(),
                block.getX(),
                block.getY(),
                block.getZ());
    }

    private static double distanceToCellExit(
            double x, double y, double z, double dx, double dy, double dz,
            double xBoundary, double yBoundary, double zBoundary) {
        return Math.min(
                distanceToBoundary(x, dx, xBoundary),
                Math.min(distanceToBoundary(y, dy, yBoundary), distanceToBoundary(z, dz, zBoundary)));
    }

    private static double distanceToCellExit(
            double x, double y, double z, double dx, double dy, double dz, int blockX, int blockY, int blockZ) {
        return Math.min(
                distanceToBoundary(x, dx, dx > 0.0 ? blockX + 1.0 : blockX),
                Math.min(
                        distanceToBoundary(y, dy, dy > 0.0 ? blockY + 1.0 : blockY),
                        distanceToBoundary(z, dz, dz > 0.0 ? blockZ + 1.0 : blockZ)));
    }

    private static double distanceToBoundary(double coordinate, double direction, double boundary) {
        if (Math.abs(direction) < 1.0e-12) {
            return Double.POSITIVE_INFINITY;
        }
        double distance = (boundary - coordinate) / direction;
        return distance >= -1.0e-9 ? Math.max(0.0, distance) : Double.POSITIVE_INFINITY;
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

    record FacePosition(BlockPosition block, BlockFace face) {}

    private record RayPath(boolean protectedFaceAbsorbed, RayTraceResult targetHit, String trace) {}

    private record FaceConsumption(
            Block block, BlockFace face, int before, int after, boolean resolved, boolean duplicate) {
        String summary() {
            return "consumed@" + position(block) + " face=" + face + " before=" + before + " after=" + after
                    + " resolved=" + resolved + " duplicate=" + duplicate;
        }
    }

    private static final class BlockAudit {
        private final Block block;
        private final org.bukkit.Material typeBefore;
        private final int[] facesBefore;
        private final int shieldBefore;
        private final List<String> rays = new ArrayList<>();
        private String decision = "intervening_ray_only";

        private BlockAudit(Block block, org.bukkit.Material typeBefore, int[] facesBefore, int shieldBefore) {
            this.block = block;
            this.typeBefore = typeBefore;
            this.facesBefore = facesBefore;
            this.shieldBefore = shieldBefore;
        }

        private Block block() {
            return block;
        }

        private org.bukkit.Material typeBefore() {
            return typeBefore;
        }

        private int[] facesBefore() {
            return facesBefore;
        }

        private int shieldBefore() {
            return shieldBefore;
        }

        private List<String> rays() {
            return rays;
        }

        private String decision() {
            return decision;
        }

        private void addRay(String ray) {
            rays.add(ray);
        }

        private void setDecision(String decision) {
            this.decision = decision;
        }
    }
}
