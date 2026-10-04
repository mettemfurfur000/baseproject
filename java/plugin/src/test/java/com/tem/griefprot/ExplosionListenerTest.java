package com.tem.griefprot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Set;
import java.util.UUID;
import org.bukkit.block.BlockFace;
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
    @DisplayName("a protected block hit between the blast and target occludes the target")
    void protectedBlockShieldsBlocksBehindIt() {
        UUID world = UUID.randomUUID();
        ExplosionListener.BlockPosition target = new ExplosionListener.BlockPosition(world, 2, 64, 0);
        ExplosionListener.BlockPosition protectedBlock = new ExplosionListener.BlockPosition(world, 1, 64, 0);

        assertTrue(ExplosionListener.isProtectedObstruction(target, protectedBlock, Set.of(protectedBlock)));
    }

    @Test
    @DisplayName("a target is not considered its own protected obstruction")
    void targetIsNotItsOwnObstruction() {
        UUID world = UUID.randomUUID();
        ExplosionListener.BlockPosition target = new ExplosionListener.BlockPosition(world, 2, 64, 0);

        assertFalse(ExplosionListener.isProtectedObstruction(target, target, Set.of(target)));
    }

    @Test
    @DisplayName("an ordinary intervening block does not count as protection occlusion")
    void unprotectedBlockDoesNotOcclude() {
        UUID world = UUID.randomUUID();
        ExplosionListener.BlockPosition target = new ExplosionListener.BlockPosition(world, 2, 64, 0);
        ExplosionListener.BlockPosition obstruction = new ExplosionListener.BlockPosition(world, 1, 64, 0);

        assertFalse(ExplosionListener.isProtectedObstruction(target, obstruction, Set.of()));
    }

}
