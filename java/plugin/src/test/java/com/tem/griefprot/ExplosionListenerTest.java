package com.tem.griefprot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.bukkit.Location;
import org.bukkit.block.BlockFace;
import org.bukkit.util.Vector;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

class ExplosionListenerTest {

    @Test
    @DisplayName("blast-facing side is selected for all six directions")
    void selectsFaceTowardOrigin() {
        assertEquals(BlockFace.EAST, ExplosionListener.faceToward(2.5, 64.5, 0.5, 0, 64, 0));
        assertEquals(BlockFace.WEST, ExplosionListener.faceToward(-1.5, 64.5, 0.5, 0, 64, 0));
        assertEquals(BlockFace.UP, ExplosionListener.faceToward(0.5, 66.5, 0.5, 0, 64, 0));
        assertEquals(BlockFace.DOWN, ExplosionListener.faceToward(0.5, 61.5, 0.5, 0, 64, 0));
        assertEquals(BlockFace.SOUTH, ExplosionListener.faceToward(0.5, 64.5, 2.5, 0, 64, 0));
        assertEquals(BlockFace.NORTH, ExplosionListener.faceToward(0.5, 64.5, -1.5, 0, 64, 0));
    }

    @Test
    @DisplayName("the dominant axis is used for diagonal blasts")
    void dominantAxisWinsForDiagonal() {
        assertEquals(BlockFace.NORTH, ExplosionListener.faceToward(1.5, 65.5, -2.5, 0, 64, 0));
    }

    @Test
    @DisplayName("an origin at the block center has no directional face")
    void centeredOriginHasNoFace() {
        assertEquals(BlockFace.SELF, ExplosionListener.faceToward(0.5, 64.5, 0.5, 0, 64, 0));
    }

    @Test
    @DisplayName("a ray leaving an integer boundary in the negative direction advances immediately")
    void rayLeavesNegativeVoxelImmediately() {
        Location origin = new Location(null, 4.0, 64.5, 0.5);
        Vector direction = new Vector(-1.0, 0.0, 0.0);

        assertEquals(0.0, ExplosionListener.distanceToCellExit(origin, direction));
    }

    @Test
    @DisplayName("a ray inside a voxel advances to the next boundary in its travel direction")
    void rayAdvancesToNextVoxelBoundary() {
        Location origin = new Location(null, 4.25, 64.5, 0.5);
        Vector direction = new Vector(-1.0, 0.0, 0.0);

        assertEquals(0.25, ExplosionListener.distanceToCellExit(origin, direction), 1.0e-9);
    }

    @Test
    @DisplayName("a ray crossing a block from west to east checks its east exit face")
    void rayChecksOppositeFaceWhenLeavingBlock() {
        assertEquals(
                List.of(BlockFace.EAST),
                ExplosionListener.exitFaces(new Vector(0.0, 64.5, 0.5), new Vector(1.0, 0.0, 0.0), 0, 64, 0));
    }

    @Test
    @DisplayName("a ray exiting exactly at an edge checks both crossed faces")
    void rayChecksBothFacesAtEdgeExit() {
        assertEquals(
                List.of(BlockFace.EAST, BlockFace.UP),
                ExplosionListener.exitFaces(new Vector(0.0, 64.0, 0.5), new Vector(1.0, 1.0, 0.0), 0, 64, 0));
    }

    @Test
    @DisplayName("one explosion consumes a given protected face at most once")
    void protectedFaceHitIsDeduplicatedPerExplosion() {
        UUID world = UUID.randomUUID();
        ExplosionListener.BlockPosition block = new ExplosionListener.BlockPosition(world, -217, 79, 67);
        Set<ExplosionListener.FacePosition> hits = new HashSet<>();

        assertTrue(ExplosionListener.markFaceHit(hits, block, BlockFace.EAST));
        assertFalse(ExplosionListener.markFaceHit(hits, block, BlockFace.EAST));
        assertTrue(ExplosionListener.markFaceHit(hits, block, BlockFace.UP));
    }

}
