package com.tem.griefprot.nativebridge;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.EnumSet;
import java.util.Set;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

/**
 * Drives the real compiled shim through the FFM bindings.
 *
 * <p>The C suite in {@code java/native/gp_abi_test.c} already proves the shim's
 * behaviour from the other side. This exists to prove the Java side of the same
 * contract: that every descriptor matches the C signature, that out parameters
 * land in the right slots, and that unsigned values survive the round trip. A
 * wrong descriptor does not fail loudly in FFM, it writes to the wrong place, so
 * this layer needs its own coverage rather than trusting the C tests.
 */
class AbiTest {

    private static Abi abi;

    private int world;

    @BeforeAll
    static void loadLibrary() {
        abi = new Abi();
    }

    @AfterEach
    void destroyWorld() {
        if (world != 0) {
            abi.worldDestroy(world);
            world = 0;
        }
    }

    /**
     * Creates a world, releasing the previous one first.
     *
     * <p>World handles are recycled slot indexes, so a leaked world would let a
     * later test alias an earlier one's data. @AfterEach only has to clean up
     * the most recent handle because this takes care of the rest.
     */
    private int newWorld() {
        if (world != 0) {
            abi.worldDestroy(world);
        }
        world = abi.worldCreate();
        assertNotEquals(0, world, "world creation failed: " + abi.lastError());
        return world;
    }

    @Test
    @DisplayName("the library agrees with the Java side on every layout assumption")
    void abiCheckPasses() {
        abi.verify(1, 0);
    }

    @Test
    @DisplayName("version is reported as major 1")
    void versionIsOne() {
        assertEquals(1, abi.getVersion() >>> 16);
    }

    @Test
    @DisplayName("face ids are distinct and there are seven including NONE")
    void faceIdsAreDistinct() {
        Set<Integer> faces = EnumSet.allOf(FaceId.class).stream()
                .map(id -> switch (id) {
                    case NONE -> abi.faceNone();
                    case DOWN -> abi.faceDown();
                    case UP -> abi.faceUp();
                    case NORTH -> abi.faceNorth();
                    case SOUTH -> abi.faceSouth();
                    case WEST -> abi.faceWest();
                    case EAST -> abi.faceEast();
                })
                .collect(java.util.stream.Collectors.toSet());
        assertEquals(7, abi.faceCount());
        assertEquals(7, faces.size(), "face ids collided: " + faces);
    }

    @Test
    @DisplayName("a destroyed world handle is reported as nothing protected")
    void destroyedWorldFailsSafe() {
        int w = abi.worldCreate();
        assertNotEquals(0, w);
        abi.worldDestroy(w);
        assertFalse(abi.reinfFaceActive(w, 0, 64, 0, abi.faceUp()));
        assertEquals(BreakResult.FAILED, abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), true, true));
    }

    @Test
    @DisplayName("handle 0 is never valid")
    void zeroHandleIsSafe() {
        assertFalse(abi.chunkAt(0, 0, 64, 0));
        assertEquals(0, abi.pointsGet(0, 0, 64, 0));
        assertEquals(Abi.NO_FACE, abi.weakestFace(0, 0, 64, 0));
    }

    @Test
    @DisplayName("config round trips through the getter")
    void configRoundTrips() {
        int w = newWorld();
        abi.worldConfig(w, 4, 16, 5, 60, 8, 32, 1000, 10, 30, 1, 320, 3, 5);

        WorldConfig config = abi.worldConfigGet(w);
        assertEquals(4, config.maxFacesPerBlock());
        assertEquals(16, config.shieldPool());
        assertEquals(60, config.shieldWindow());
        assertEquals(320, config.chunkHeight());
        assertEquals(3, config.copperBreaks());
        assertEquals(5, config.ironBreaks());
    }

    @Test
    @DisplayName("KEEP leaves a field at the library default")
    void keepPreservesDefaults() {
        int w = newWorld();
        abi.worldConfig(w, 4, 16, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP,
                Abi.KEEP, 320, Abi.KEEP, Abi.KEEP);

        WorldConfig config = abi.worldConfigGet(w);
        assertEquals(4, config.maxFacesPerBlock(), "explicit value should be kept");
        assertEquals(320, config.chunkHeight(), "explicit value should be kept");
        assertNotEquals(0, config.shieldWindow(), "KEEP should retain the default");
    }

    @Test
    @DisplayName("chunk residency is reported and can be released")
    void chunkLifecycle() {
        int w = newWorld();
        assertFalse(abi.chunkIsLoaded(w, 0, 0));
        assertTrue(abi.chunkAt(w, 3, 64, 5));
        assertTrue(abi.chunkIsLoaded(w, 0, 0));

        ChunkCounts counts = abi.chunkCounts(w);
        assertEquals(1, counts.loaded());
        assertTrue(abi.chunkUnload(w, 0, 0));
        assertFalse(abi.chunkUnload(w, 0, 0), "unloading twice should report nothing to do");
    }

    @Test
    @DisplayName("chunk queries never allocate an unloaded chunk")
    void unloadedChunkIsNotCreatedByQueries() {
        int w = newWorld();
        abi.pointsGet(w, 1, 64, 1);
        abi.weakestFace(w, 1, 64, 1);
        abi.chunkCovered(w, 1, 64, 1, 0L);
        abi.chunkDecay(w, 1, 64, 1, 0L, 10, 1);
        assertFalse(abi.chunkIsLoaded(w, 0, 0), "a read-only query must not load a chunk");
    }

    @Test
    @DisplayName("reinforcement is upgrade only")
    void reinforcementIsUpgradeOnly() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);

        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 3));
        assertEquals(3, abi.reinfFaceDurability(w, 0, 64, 0, abi.faceUp()));

        // Same tier again must not add to the first.
        assertFalse(abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 3));
        assertEquals(3, abi.reinfFaceDurability(w, 0, 64, 0, abi.faceUp()));

        // Stronger tier replaces in place.
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 8));
        assertEquals(8, abi.reinfFaceDurability(w, 0, 64, 0, abi.faceUp()));

        // Zero is always refused.
        assertFalse(abi.reinfAdd(w, 0, 64, 0, abi.faceDown(), 0));
        assertFalse(abi.reinfFaceActive(w, 0, 64, 0, abi.faceDown()));
    }

    @Test
    @DisplayName("the weakest face is the one an unlabelled break resolves against")
    void weakestFaceTracksDurability() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 8);
        abi.reinfAdd(w, 0, 64, 0, abi.faceNorth(), 2);

        assertEquals(abi.faceNorth(), abi.weakestFace(w, 0, 64, 0));
        assertEquals(2, abi.reinfFaceCount(w, 0, 64, 0));

        // Reinforcement is per face, so clearing the block drops everything.
        assertTrue(abi.reinfClear(w, 0, 64, 0));
        assertEquals(Abi.NO_FACE, abi.weakestFace(w, 0, 64, 0));
    }

    @Test
    @DisplayName("a reinforced face absorbs a break and pays durability for it")
    void reinforcedFaceSurvivesBreak() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 40);

        BreakResult result = abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), true, false);
        assertTrue(result.reinforcementHeld());
        assertTrue(result.protectedFromBreak());
        assertFalse(result.destroyed());
        assertEquals(39, abi.reinfFaceDurability(w, 0, 64, 0, abi.faceUp()));
    }

    @Test
    @DisplayName("the last point of durability does not save the block")
    void lastDurabilityDoesNotSaveTheBlock() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 1);

        // The library spends durability before deciding, so one point absorbs
        // zero breaks. Worth pinning down: a host sizing "one hit to place, one
        // hit to break" would be wrong here.
        BreakResult result = abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), true, false);
        assertFalse(result.reinforcementHeld());
        assertTrue(result.destroyed());
        assertFalse(abi.reinfFaceActive(w, 0, 64, 0, abi.faceUp()), "the face should be spent");
        assertEquals(0, abi.reinfFaceCount(w, 0, 64, 0));
    }

    @Test
    @DisplayName("an unlabelled break on a reinforced block is treated as aimed at the weakest face")
    void weakFaceBreakGetsThrough() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 8);
        abi.reinfAdd(w, 0, 64, 0, abi.faceDown(), 1);

        int weakest = abi.weakestFace(w, 0, 64, 0);
        BreakResult result = abi.resolveBreak(w, 0, 64, 0, weakest, true, false);
        assertTrue(result.destroyed(), "the weak face should not have protected the block");
    }

    @Test
    @DisplayName("shield points absorb a break and are consumed")
    void shieldPointsAbsorbBreak() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.pointsAdd(w, 0, 64, 0, 4, 16);
        assertEquals(4, abi.pointsGet(w, 0, 64, 0));

        BreakResult result = abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), false, true);
        assertFalse(result.reinforcementHeld(), "no face was reinforced here");
        assertTrue(result.shieldPaid(), "a point should have paid for the break");
        assertTrue(result.protectedFromBreak());
        assertFalse(result.destroyed());
        assertEquals(1, result.absorbed());
        assertEquals(3, abi.pointsGet(w, 0, 64, 0));
    }

    @Test
    @DisplayName("a block with no points left does break")
    void exhaustedPointsLetTheBlockBreak() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.pointsAdd(w, 0, 64, 0, 1, 16);

        assertTrue(abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), false, true).protectedFromBreak());
        BreakResult second = abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), false, true);
        assertTrue(second.destroyed());
        assertEquals(0, abi.pointsGet(w, 0, 64, 0));
    }

    @Test
    @DisplayName("switching a layer off stops it protecting anything")
    void disabledLayersDoNotProtect() {
        // Separate worlds so each layer is observed without the other call
        // having already spent from the same block.
        int reinfOnly = newWorld();
        abi.chunkAt(reinfOnly, 0, 64, 0);
        abi.reinfAdd(reinfOnly, 0, 64, 0, abi.faceUp(), 40);
        abi.pointsAdd(reinfOnly, 0, 64, 0, 4, 16);

        BreakResult noReinf = abi.resolveBreak(reinfOnly, 0, 64, 0, abi.faceUp(), false, true);
        assertFalse(noReinf.reinforcementHeld(), "reinforcement was switched off");
        assertEquals(40, abi.reinfFaceDurability(reinfOnly, 0, 64, 0, abi.faceUp()), "a disabled face must not be spent");
        assertEquals(3, abi.pointsGet(reinfOnly, 0, 64, 0), "the enabled layer should have paid instead");

        int shieldOnly = newWorld();
        abi.chunkAt(shieldOnly, 0, 64, 0);
        abi.reinfAdd(shieldOnly, 0, 64, 0, abi.faceUp(), 40);
        abi.pointsAdd(shieldOnly, 0, 64, 0, 4, 16);

        BreakResult noShield = abi.resolveBreak(shieldOnly, 0, 64, 0, abi.faceUp(), true, false);
        assertTrue(noShield.reinforcementHeld());
        assertEquals(39, abi.reinfFaceDurability(shieldOnly, 0, 64, 0, abi.faceUp()));
        assertEquals(4, abi.pointsGet(shieldOnly, 0, 64, 0), "a disabled layer must not be spent");
    }

    @Test
    @DisplayName("reinforcement is consulted before shield points")
    void reinforcementPrecedesShields() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 8);
        abi.pointsAdd(w, 0, 64, 0, 4, 16);

        BreakResult result = abi.resolveBreak(w, 0, 64, 0, abi.faceUp(), true, true);
        assertTrue(result.protectedFromBreak());
        assertEquals(0, result.absorbed(), "shields should not be spent while a face still holds");
        assertEquals(4, abi.pointsGet(w, 0, 64, 0));
    }

    @Test
    @DisplayName("taking points from an unloaded chunk spends nothing and loads nothing")
    void takingPointsDoesNotCreateChunks() {
        int w = newWorld();
        assertEquals(0, abi.pointsTake(w, 5, 64, 5, 4));
        assertFalse(abi.chunkIsLoaded(w, 0, 0));
    }

    @Test
    @DisplayName("adding points does load the chunk, because it is about to write to it")
    void addingPointsLoadsTheChunk() {
        int w = newWorld();
        assertEquals(4, abi.pointsAdd(w, 5, 64, 5, 4, 16));
        assertTrue(abi.chunkIsLoaded(w, 0, 0));
        assertEquals(4, abi.pointsGet(w, 5, 64, 5));
    }

    @Test
    @DisplayName("protection travels with a block that a piston moves")
    void movementCarriesProtection() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 1, 64, 0, abi.faceUp(), 8);

        assertTrue(abi.moveBlock(w, 1, 64, 0, 2, 64, 0, abi.faceEast()));
        assertEquals(0, abi.reinfFaceCount(w, 1, 64, 0), "source should be empty");
        assertEquals(8, abi.reinfFaceDurability(w, 2, 64, 0, abi.faceUp()));
    }

    @Test
    @DisplayName("a shield reports its position and can be ticked")
    void shieldTicks() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);

        int shield = abi.shieldCreate(w, 0, 64, 0);
        assertNotEquals(0, shield, "shield creation failed: " + abi.lastError());
        assertEquals(new Pos(0, 64, 0), abi.shieldGetPos(shield));
        assertEquals(1, abi.shieldCount(w));

        ShieldState initial = abi.shieldGetState(shield);
        abi.shieldSetPowered(shield, true);
        assertTrue(abi.shieldGetState(shield).powered());

        // The first tick anchors the clock rather than charging for time that
        // never ran, so a shield restored from disk does not burn its whole tank
        // on the tick after a restart.
        assertEquals(0, abi.shieldTick(shield, 100L));
        assertEquals(initial.fuel(), abi.shieldGetState(shield).fuel(), "the anchoring tick should cost no fuel");

        abi.shieldTick(shield, 105L);
        assertTrue(
                abi.shieldGetState(shield).fuel() < initial.fuel(),
                "a running shield should burn fuel once its window is open");

        assertEquals(shield, abi.shieldAt(w, 0));
        assertEquals(0, abi.shieldAt(w, 99), "past the end should report no shield");

        abi.shieldDestroy(shield);
        assertEquals(0, abi.shieldCount(w));
    }

    @Test
    @DisplayName("cover holds up shield points until one interval past now")
    void coverDelaysDecay() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.pointsAdd(w, 0, 64, 0, 4, 16);

        // Cover applies to shield points, not to reinforcement.
        assertEquals(0, abi.chunkCovered(w, 0, 64, 0, 100L));
        abi.cover(w, 0, 64, 0, 100L);
        assertEquals(1, abi.chunkCovered(w, 0, 64, 0, 100L));

        abi.chunkDecay(w, 0, 64, 0, 110L, 30, 1);
        assertEquals(4, abi.pointsGet(w, 0, 64, 0), "a covered block should keep its points");

        // The grace window is set from the world's own decay interval, not the
        // interval passed to the sweep, so read it rather than guessing.
        long interval = abi.worldConfigGet(w).decayInterval();
        long lapsed = 100L + interval + 2L;
        assertEquals(0, abi.chunkCovered(w, 0, 64, 0, lapsed));

        DecayResult after = abi.chunkDecay(w, 0, 64, 0, lapsed, (int) interval, 1);
        assertTrue(after.pointsRemoved() > 0, "decay should have taken the points");
    }

    @Test
    @DisplayName("cover ignores reinforcement, which does not decay")
    void coverDoesNotApplyToReinforcement() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 8);

        abi.cover(w, 0, 64, 0, 100L);
        assertEquals(0, abi.chunkCovered(w, 0, 64, 0, 100L), "no points, nothing to hold up");
        abi.chunkDecay(w, 0, 64, 0, 400L, 30, 1);
        assertEquals(8, abi.reinfFaceDurability(w, 0, 64, 0, abi.faceUp()));
    }

    @Test
    @DisplayName("a world survives a save and load round trip")
    void persistenceRoundTrips() throws Exception {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);
        abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 6);
        abi.pointsAdd(w, 1, 64, 0, 3, 16);

        Path file = Files.createTempFile("griefprot-abi-test", ".gpdata");
        assertTrue(abi.worldSave(w, file.toString()), "save failed: " + abi.lastError());
        assertTrue(Files.size(file) > 0);

        int restored = newWorld();
        int shields = abi.worldLoad(restored, file.toString(), 16, 32);
        assertTrue(shields >= 0, "load failed: " + abi.lastError());

        abi.chunkAt(restored, 0, 64, 0);
        assertEquals(6, abi.reinfFaceDurability(restored, 0, 64, 0, abi.faceUp()));
        assertEquals(3, abi.pointsGet(restored, 1, 64, 0));

        Files.deleteIfExists(file);
    }

    @Test
    @DisplayName("loading a missing file fails without corrupting the world")
    void missingFileIsReported() {
        int w = newWorld();
        assertEquals(-1, abi.worldLoad(w, "no-such-file.gpdata", 16, 32));
        assertNotEquals(null, abi.lastError(), "a failed load should leave an error message");
    }

    @Test
    @DisplayName("a block carries at most four distinct faces")
    void faceSlotLimitIsEnforced() {
        int w = newWorld();
        abi.chunkAt(w, 0, 64, 0);

        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 1));
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceDown(), 1));
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceNorth(), 1));
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceSouth(), 1));
        assertEquals(4, abi.reinfFaceCount(w, 0, 64, 0));

        // The record is full; the library must refuse rather than drop a face.
        assertFalse(abi.reinfAdd(w, 0, 64, 0, abi.faceEast(), 1));
        assertEquals(4, abi.reinfFaceCount(w, 0, 64, 0));
    }

    @Test
    @DisplayName("maxFacesPerBlock gates a piston merge, not a fresh placement")
    void faceCapAppliesToMerges() {
        int w = newWorld();
        abi.worldConfig(w, 2, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP, Abi.KEEP,
                Abi.KEEP, Abi.KEEP, 320, Abi.KEEP, Abi.KEEP);
        abi.chunkAt(w, 0, 64, 0);

        // Placing on a fresh block is bounded by the four record slots, not by
        // the configured cap.
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceUp(), 1));
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceNorth(), 1));
        assertTrue(abi.reinfAdd(w, 0, 64, 0, abi.faceEast(), 1));
        assertEquals(3, abi.reinfFaceCount(w, 0, 64, 0));

        // Moving it onto a block that already has faces would exceed the cap, so
        // the move is refused and nothing is transferred.
        abi.reinfAdd(w, 1, 64, 0, abi.faceWest(), 1);
        assertFalse(abi.moveBlock(w, 0, 64, 0, 1, 64, 0, abi.faceEast()));
        assertEquals(3, abi.reinfFaceCount(w, 0, 64, 0));
        assertEquals(1, abi.reinfFaceCount(w, 1, 64, 0));
    }

    @Test
    @DisplayName("a position outside the world height is refused rather than clamped")
    void outOfRangeIsRejected() {
        int w = newWorld();
        assertFalse(abi.chunkAt(w, 0, 10_000, 0), "above the world height");
        assertFalse(abi.chunkIsLoaded(w, 0, 0));
        assertFalse(abi.reinfAdd(w, 0, 10_000, 0, abi.faceUp(), 1));
    }

    /** Face ids that exist only so the distinctness test can enumerate them. */
    private enum FaceId {
        NONE,
        DOWN,
        UP,
        NORTH,
        SOUTH,
        WEST,
        EAST
    }
}