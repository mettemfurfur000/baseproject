package com.tem.griefprot.nativebridge;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.util.ArrayList;
import java.util.List;

/**
 * Typed Java view of {@code gp_abi.h}.
 *
 * <p>Every downcall is bound once here. Structs never cross this boundary: the
 * shim exposes only {@code uint32} handles, scalars, and caller allocated out
 * parameters, so there is no layout to get wrong and no {@code GroupLayout} to
 * mirror against the C compiler's idea of padding.
 *
 * <p>Out parameters are read through a short lived {@link Arena#ofConfined()}
 * per call. All of this is reached from the server's main thread, so the
 * confinement is free; the arena is just a scoped allocation rather than a
 * lifetime the caller has to manage.
 *
 * <p>Handles are {@code int}s carrying unsigned values. They are indexes plus
 * one, so they stay comfortably positive and {@code 0} means "invalid". They are
 * <em>not</em> generation tagged, which is the one sharp edge in this API: a
 * handle kept past the world it named will silently refer to whatever took that
 * slot next. Callers must therefore drop handles when a world is destroyed
 * rather than caching them.
 */
public final class Abi {

    /** Sentinel meaning "leave this field at the library default" ({@code 0xFFFFFFFF}). */
    public static final int KEEP = -1;

    /** Sentinel returned by {@link #weakestFace} for a block with no reinforcement. */
    public static final int NO_FACE = -1;

    private static final ValueLayout ADDRESS = ValueLayout.ADDRESS;

    // Check bits, mirrored from gp_abi.h so Java does not hardcode the numbers.
    public static final int CHECK_PTR_SIZE = 1;
    public static final int CHECK_U32_SIZE = 1 << 1;
    public static final int CHECK_U64_SIZE = 1 << 2;
    public static final int CHECK_POS_SIZE = 1 << 3;
    public static final int CHECK_BOOL_SIZE = 1 << 4;
    public static final int CHECK_FACE_VALUES = 1 << 5;
    public static final int CHECK_BREAK_RESULT_SIZE = 1 << 6;
    public static final int CHECK_POS_IS_3XI32 = 1 << 7;

    private final Linker linker;
    private final SymbolLookup lookup;

    // --- lifecycle ---
    private final MethodHandle check;
    private final MethodHandle expect;
    private final MethodHandle checkName;
    private final MethodHandle version;

    // --- faces ---
    private final MethodHandle faceNone;
    private final MethodHandle faceDown;
    private final MethodHandle faceUp;
    private final MethodHandle faceNorth;
    private final MethodHandle faceSouth;
    private final MethodHandle faceWest;
    private final MethodHandle faceEast;
    private final MethodHandle faceCount;

    // --- worlds ---
    private final MethodHandle worldCreate;
    private final MethodHandle worldDestroy;
    private final MethodHandle worldConfig;
    private final MethodHandle worldConfigGet;
    private final MethodHandle worldMemory;

    // --- chunks ---
    private final MethodHandle chunkAt;
    private final MethodHandle chunkIsLoaded;
    private final MethodHandle chunkUnload;
    private final MethodHandle chunkCounts;
    private final MethodHandle chunkDecay;
    private final MethodHandle chunkCovered;

    // --- reinforcement ---
    private final MethodHandle reinfAdd;
    private final MethodHandle reinfFaceDurability;
    private final MethodHandle reinfFaceActive;
    private final MethodHandle reinfClear;
    private final MethodHandle reinfFaceCount;
    private final MethodHandle weakestFace;

    // --- break ---
    private final MethodHandle resolveBreak;

    // --- points ---
    private final MethodHandle pointsGet;
    private final MethodHandle pointsAdd;
    private final MethodHandle pointsTake;

    // --- movement ---
    private final MethodHandle moveBlock;

    // --- sink ---
    private final MethodHandle sinkConfigure;
    private final MethodHandle cover;

    // --- shields ---
    private final MethodHandle shieldCreate;
    private final MethodHandle shieldDestroy;
    private final MethodHandle shieldAddTarget;
    private final MethodHandle shieldRemoveTarget;
    private final MethodHandle shieldClearTargets;
    private final MethodHandle shieldSetPowered;
    private final MethodHandle shieldSetFuel;
    private final MethodHandle shieldGetState;
    private final MethodHandle shieldGetPos;
    private final MethodHandle shieldTick;
    private final MethodHandle shieldAt;
    private final MethodHandle shieldCount;

    // --- persistence ---
    private final MethodHandle worldSave;
    private final MethodHandle worldLoad;
    private final MethodHandle lastError;

    // Face ids are asked of the library rather than assumed, so a future C enum
    // reshuffle cannot silently make the Java constants wrong.
    private final int fNone;
    private final int fDown;
    private final int fUp;
    private final int fNorth;
    private final int fSouth;
    private final int fWest;
    private final int fEast;
    private final int fCount;

    public Abi() {
        NativeLib lib = NativeLib.get();
        this.linker = lib.linker();
        this.lookup = lib.lookup();

        this.check = down(linker, lookup, "gp_abi_check", FunctionDescriptor.of(JAVA_INT));
        this.expect = down(linker, lookup, "gp_abi_expect", FunctionDescriptor.of(JAVA_LONG, JAVA_INT));
        this.checkName = down(linker, lookup, "gp_abi_check_name", FunctionDescriptor.of(ADDRESS, JAVA_INT));
        this.version = down(linker, lookup, "gp_abi_version", FunctionDescriptor.of(JAVA_INT));

        FunctionDescriptor face = FunctionDescriptor.of(JAVA_INT);
        this.faceNone = down(linker, lookup, "gp_abi_face_none", face);
        this.faceDown = down(linker, lookup, "gp_abi_face_down", face);
        this.faceUp = down(linker, lookup, "gp_abi_face_up", face);
        this.faceNorth = down(linker, lookup, "gp_abi_face_north", face);
        this.faceSouth = down(linker, lookup, "gp_abi_face_south", face);
        this.faceWest = down(linker, lookup, "gp_abi_face_west", face);
        this.faceEast = down(linker, lookup, "gp_abi_face_east", face);
        this.faceCount = down(linker, lookup, "gp_abi_face_count", face);

        this.worldCreate = down(linker, lookup, "gp_abi_world_create", face);
        this.worldDestroy =
                down(linker, lookup, "gp_abi_world_destroy", FunctionDescriptor.ofVoid(JAVA_INT));
        this.worldConfig = down(
                linker,
                lookup,
                "gp_abi_world_config",
                FunctionDescriptor.ofVoid(concat(JAVA_INT, unpack(JAVA_INT, CONFIG_FIELDS))));
        this.worldConfigGet = down(
                linker,
                lookup,
                "gp_abi_world_config_get",
                FunctionDescriptor.ofVoid(concat(JAVA_INT, unpack(ADDRESS, CONFIG_FIELDS))));
        this.worldMemory = down(linker, lookup, "gp_abi_world_memory", FunctionDescriptor.of(JAVA_LONG, JAVA_INT));

        this.chunkAt = down(linker, lookup, "gp_abi_chunk_at", FunctionDescriptor.of(JAVA_INT, w3()));
        this.chunkIsLoaded = down(linker, lookup, "gp_abi_chunk_is_loaded", FunctionDescriptor.of(JAVA_INT, w2()));
        this.chunkUnload = down(linker, lookup, "gp_abi_chunk_unload", FunctionDescriptor.of(JAVA_INT, w2()));
        this.chunkCounts = down(
                linker, lookup, "gp_abi_chunk_counts", FunctionDescriptor.ofVoid(JAVA_INT, ADDRESS, ADDRESS));
        this.chunkDecay = down(
                linker,
                lookup,
                "gp_abi_chunk_decay",
                FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG, JAVA_INT, JAVA_INT, ADDRESS, ADDRESS));
        this.chunkCovered = down(
                linker, lookup, "gp_abi_chunk_covered", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG));

        this.reinfAdd = down(
                linker, lookup, "gp_abi_reinf_add", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));
        this.reinfFaceDurability = down(
                linker, lookup, "gp_abi_reinf_face_durability", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));
        this.reinfFaceActive = down(
                linker, lookup, "gp_abi_reinf_face_active", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));
        this.reinfClear = down(linker, lookup, "gp_abi_reinf_clear", FunctionDescriptor.of(JAVA_INT, w3()));
        this.reinfFaceCount = down(linker, lookup, "gp_abi_reinf_face_count", FunctionDescriptor.of(JAVA_INT, w3()));
        this.weakestFace = down(linker, lookup, "gp_abi_weakest_face", FunctionDescriptor.of(JAVA_INT, w3()));

        this.resolveBreak = down(
                linker,
                lookup,
                "gp_abi_resolve_break",
                FunctionDescriptor.of(
                        JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS));

        this.pointsGet = down(linker, lookup, "gp_abi_points_get", FunctionDescriptor.of(JAVA_INT, w3()));
        this.pointsAdd = down(
                linker, lookup, "gp_abi_points_add", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));
        this.pointsTake = down(
                linker, lookup, "gp_abi_points_take", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));

        this.moveBlock = down(
                linker, lookup, "gp_abi_move_block", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));

        this.sinkConfigure = down(
                linker, lookup, "gp_abi_sink_configure", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_LONG, JAVA_LONG, JAVA_LONG, JAVA_LONG));
        this.cover = down(
                linker, lookup, "gp_abi_cover", FunctionDescriptor.ofVoid(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG));

        this.shieldCreate = down(linker, lookup, "gp_abi_shield_create", FunctionDescriptor.of(JAVA_INT, w3()));
        this.shieldDestroy = down(linker, lookup, "gp_abi_shield_destroy", FunctionDescriptor.ofVoid(JAVA_INT));
        this.shieldAddTarget = down(linker, lookup, "gp_abi_shield_add_target", FunctionDescriptor.of(JAVA_INT, w3()));
        this.shieldRemoveTarget = down(linker, lookup, "gp_abi_shield_remove_target", FunctionDescriptor.of(JAVA_INT, w3()));
        this.shieldClearTargets = down(linker, lookup, "gp_abi_shield_clear_targets", FunctionDescriptor.ofVoid(JAVA_INT));
        this.shieldSetPowered = down(linker, lookup, "gp_abi_shield_set_powered", FunctionDescriptor.ofVoid(JAVA_INT, JAVA_INT));
        this.shieldSetFuel = down(linker, lookup, "gp_abi_shield_set_fuel", FunctionDescriptor.ofVoid(JAVA_INT, JAVA_INT));
        this.shieldGetState = down(
                linker, lookup, "gp_abi_shield_get_state", FunctionDescriptor.ofVoid(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
        this.shieldGetPos = down(
                linker, lookup, "gp_abi_shield_get_pos", FunctionDescriptor.ofVoid(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
        this.shieldTick = down(
                linker, lookup, "gp_abi_shield_tick", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_LONG, ADDRESS));
        this.shieldAt = down(linker, lookup, "gp_abi_shield_at", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT));
        this.shieldCount = down(linker, lookup, "gp_abi_shield_count", FunctionDescriptor.of(JAVA_INT, JAVA_INT));

        this.worldSave = down(linker, lookup, "gp_abi_world_save", FunctionDescriptor.of(JAVA_INT, JAVA_INT, ADDRESS));
        this.worldLoad = down(linker, lookup, "gp_abi_world_load", FunctionDescriptor.of(JAVA_INT, JAVA_INT, ADDRESS, JAVA_INT, JAVA_INT));
        this.lastError = down(linker, lookup, "gp_abi_last_error", FunctionDescriptor.of(ADDRESS));

        this.fNone = callInt(faceNone);
        this.fDown = callInt(faceDown);
        this.fUp = callInt(faceUp);
        this.fNorth = callInt(faceNorth);
        this.fSouth = callInt(faceSouth);
        this.fWest = callInt(faceWest);
        this.fEast = callInt(faceEast);
        this.fCount = callInt(faceCount);
    }

    // --- descriptor helpers ---------------------------------------------------

    private static ValueLayout[] w3() {
        return new ValueLayout[] {JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT};
    }

    private static ValueLayout[] w2() {
        return new ValueLayout[] {JAVA_INT, JAVA_INT, JAVA_INT};
    }

    /** The 13 configurable fields, in the order gp_abi.h lists them. */
    private static final int CONFIG_FIELDS = 13;

    private static ValueLayout[] unpack(ValueLayout layout, int count) {
        ValueLayout[] out = new ValueLayout[count];
        java.util.Arrays.fill(out, layout);
        return out;
    }

    private static ValueLayout[] concat(ValueLayout first, ValueLayout[] rest) {
        ValueLayout[] out = new ValueLayout[rest.length + 1];
        out[0] = first;
        System.arraycopy(rest, 0, out, 1, rest.length);
        return out;
    }

    private MethodHandle down(Linker linker, SymbolLookup lookup, String name, FunctionDescriptor descriptor) {
        MemorySegment symbol =
                lookup.find(name).orElseThrow(() -> new UnsatisfiedLinkError("missing export: " + name));
        return linker.downcallHandle(symbol, descriptor);
    }

    // --- startup --------------------------------------------------------------

    /**
     * Verifies the C side matches what this file assumes, and that the library is
     * the version the Java side was written against.
     *
     * @throws IllegalStateException with a per check diff if anything is off
     */
    public void verify(int expectedMajor, int expectedMinor) {
        int failed;
        try {
            failed = (int) check.invokeExact();
        } catch (Throwable t) {
            throw new IllegalStateException("gp_abi_check failed to run", t);
        }
        if (failed != 0) {
            throw new IllegalStateException("griefprot ABI mismatch: " + describeFailedChecks(failed));
        }

        int packed = getVersion();
        int major = packed >>> 16;
        int minor = packed & 0xFFFF;
        if (major != expectedMajor) {
            throw new IllegalStateException(
                    "griefprot native library is version " + major + "." + minor + " but the plugin needs "
                            + expectedMajor + ".x. Rebuild it with: make -C griefprot abi");
        }
    }

    private String describeFailedChecks(int mask) {
        List<String> parts = new ArrayList<>();
        for (int bit = 0; bit < 32; bit++) {
            if ((mask & (1 << bit)) == 0) {
                continue;
            }
            String name = null;
            long expected = 0;
            try {
                MemorySegment segment = (MemorySegment) checkName.invokeExact(1 << bit);
                name = segment == null ? null : readString(segment.address());
                expected = (long) expect.invokeExact(1 << bit);
            } catch (Throwable ignored) {
                // A broken call here must not mask the mismatch being reported.
            }
            parts.add((name == null ? "bit " + bit : name) + " (expected " + expected + ")");
        }
        return String.join("; ", parts);
    }

    /** Library version packed as {@code (major << 16) | minor}. */
    public int getVersion() {
        try {
            return (int) version.invokeExact();
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /** Last error from the calling thread, or {@code null} if it has not failed. */
    public String lastError() {
        try {
            // An ADDRESS return arrives as a MemorySegment, not a raw long.
            MemorySegment segment = (MemorySegment) lastError.invokeExact();
            return segment == null ? null : readString(segment.address());
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /**
     * Reads a NUL terminated C string.
     *
     * <p>Callers normally get addresses from a {@code const char *} return, which
     * FFM hands back as a {@link MemorySegment} pointing straight at the first
     * byte, so its {@code address()} is the address to pass here.
     */
    public static String readString(long address) {
        if (address == 0L) {
            return null;
        }
        // Reinterpret to the max so the segment covers whatever strlen finds;
        // getString stops at the NUL terminator.
        return MemorySegment.ofAddress(address).reinterpret(Long.MAX_VALUE).getString(0);
    }

    // --- faces ----------------------------------------------------------------

    public int faceNone() {
        return fNone;
    }

    public int faceDown() {
        return fDown;
    }

    public int faceUp() {
        return fUp;
    }

    public int faceNorth() {
        return fNorth;
    }

    public int faceSouth() {
        return fSouth;
    }

    public int faceWest() {
        return fWest;
    }

    public int faceEast() {
        return fEast;
    }

    /** Total number of faces including the NONE sentinel. */
    public int faceCount() {
        return fCount;
    }

    // --- world lifecycle ------------------------------------------------------

    public int worldCreate() {
        try {
            return (int) worldCreate.invokeExact();
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public void worldDestroy(int world) {
        try {
            worldDestroy.invokeExact(world);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /**
     * Applies configuration, overriding only the fields that are not
     * {@link #KEEP}. A value of {@code KEEP} means "library default".
     */
    public void worldConfig(
            int world,
            int maxFacesPerBlock,
            int shieldPool,
            int shieldRegen,
            int shieldWindow,
            int shieldRange,
            int shieldMaxTargets,
            int shieldFuelMax,
            int shieldFuelPerSec,
            int decayInterval,
            int decayAmount,
            int chunkHeight,
            int copperBreaks,
            int ironBreaks) {
        try {
            worldConfig.invokeExact(
                    world, maxFacesPerBlock, shieldPool, shieldRegen, shieldWindow, shieldRange,
                    shieldMaxTargets, shieldFuelMax, shieldFuelPerSec, decayInterval, decayAmount,
                    chunkHeight, copperBreaks, ironBreaks);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /** Reads back the configuration actually in force. */
    public WorldConfig worldConfigGet(int world) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment[] out = new MemorySegment[CONFIG_FIELDS];
            for (int i = 0; i < out.length; i++) {
                out[i] = arena.allocate(JAVA_INT);
            }
            worldConfigGet.invokeExact(
                    world, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9],
                    out[10], out[11], out[12]);
            return new WorldConfig(
                    getInt(out[0]), getInt(out[1]), getInt(out[2]), getInt(out[3]), getInt(out[4]),
                    getInt(out[5]), getInt(out[6]), getInt(out[7]), getInt(out[8]), getInt(out[9]),
                    getInt(out[10]), getInt(out[11]), getInt(out[12]));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public long worldMemory(int world) {
        try {
            return (long) worldMemory.invokeExact(world);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- chunks ---------------------------------------------------------------

    public boolean chunkAt(int world, int x, int y, int z) {
        try {
            return (int) chunkAt.invokeExact(world, x, y, z) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean chunkIsLoaded(int world, int cx, int cz) {
        try {
            return (int) chunkIsLoaded.invokeExact(world, cx, cz) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean chunkUnload(int world, int cx, int cz) {
        try {
            return (int) chunkUnload.invokeExact(world, cx, cz) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public ChunkCounts chunkCounts(int world) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment loaded = arena.allocate(JAVA_INT);
            MemorySegment reserved = arena.allocate(JAVA_INT);
            chunkCounts.invokeExact(world, loaded, reserved);
            return new ChunkCounts(getInt(loaded), getInt(reserved));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public DecayResult chunkDecay(int world, int x, int y, int z, long now, int interval, int amount) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment touched = arena.allocate(JAVA_INT);
            MemorySegment skipped = arena.allocate(JAVA_INT);
            int removed = (int) chunkDecay.invokeExact(world, x, y, z, now, interval, amount, touched, skipped);
            return new DecayResult(Integer.toUnsignedLong(removed), getInt(touched), getInt(skipped));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int chunkCovered(int world, int x, int y, int z, long now) {
        try {
            return (int) chunkCovered.invokeExact(world, x, y, z, now);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- reinforcement --------------------------------------------------------

    /**
     * Applies {@code amount} break events to one face.
     *
     * <p>Upgrade only: an equal or weaker tier is refused and leaves the existing
     * record untouched. A stronger tier replaces the remaining durability in
     * place. Nothing accumulates.
     */
    public boolean reinfAdd(int world, int x, int y, int z, int face, int amount) {
        try {
            return (int) reinfAdd.invokeExact(world, x, y, z, face, amount) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int reinfFaceDurability(int world, int x, int y, int z, int face) {
        try {
            return (int) reinfFaceDurability.invokeExact(world, x, y, z, face);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean reinfFaceActive(int world, int x, int y, int z, int face) {
        try {
            return (int) reinfFaceActive.invokeExact(world, x, y, z, face) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean reinfClear(int world, int x, int y, int z) {
        try {
            return (int) reinfClear.invokeExact(world, x, y, z) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int reinfFaceCount(int world, int x, int y, int z) {
        try {
            return (int) reinfFaceCount.invokeExact(world, x, y, z);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /**
     * The reinforced face with the least durability left, or {@link #NO_FACE}.
     * Used when the server did not report which face was struck.
     */
    public int weakestFace(int world, int x, int y, int z) {
        try {
            return (int) weakestFace.invokeExact(world, x, y, z);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- break ----------------------------------------------------------------

    /**
     * Resolves one break.
     *
     * @param useReinf whether reinforced faces participate
     * @param useShield whether shield points participate
     */
    public BreakResult resolveBreak(
            int world, int x, int y, int z, int face, boolean useReinf, boolean useShield) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment survived = arena.allocate(JAVA_INT);
            MemorySegment broke = arena.allocate(JAVA_INT);
            MemorySegment absorbed = arena.allocate(JAVA_INT);
            MemorySegment left = arena.allocate(JAVA_INT);
            int ok = (int) resolveBreak.invokeExact(
                    world, x, y, z, face, bit(useReinf), bit(useShield), survived, broke, absorbed, left);
            if (ok == 0) {
                return BreakResult.FAILED;
            }
            return new BreakResult(true, getInt(survived), getInt(broke), getInt(absorbed), getInt(left));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- points ---------------------------------------------------------------

    public int pointsGet(int world, int x, int y, int z) {
        try {
            return (int) pointsGet.invokeExact(world, x, y, z);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int pointsAdd(int world, int x, int y, int z, int amount, int cap) {
        try {
            return (int) pointsAdd.invokeExact(world, x, y, z, amount, cap);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int pointsTake(int world, int x, int y, int z, int amount) {
        try {
            return (int) pointsTake.invokeExact(world, x, y, z, amount);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- movement -------------------------------------------------------------

    /**
     * Moves one block's protection.
     *
     * @param motion face id for the direction the block travels, matching
     *     {@code BlockPistonExtendEvent#getDirection}
     */
    public boolean moveBlock(int world, int fromX, int fromY, int fromZ, int toX, int toY, int toZ, int motion) {
        try {
            return (int) moveBlock.invokeExact(world, fromX, fromY, fromZ, toX, toY, toZ, motion) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- sink -----------------------------------------------------------------

    /**
     * Installs the host callbacks the library uses to decide what may be
     * protected and to hand out points.
     *
     * @param protectableAddress FFM upcall stub address, or 0 to clear
     * @param grantAddress FFM upcall stub address, or 0 to clear
     * @param coverAddress FFM upcall stub address, or 0 to clear
     */
    public boolean sinkConfigure(
            int world, long hostId, long protectableAddress, long grantAddress, long coverAddress) {
        try {
            return (int) sinkConfigure.invokeExact(world, hostId, protectableAddress, grantAddress, coverAddress) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /** Marks a block covered until one decay interval past {@code now}. */
    public void cover(int world, int x, int y, int z, long now) {
        try {
            cover.invokeExact(world, x, y, z, now);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- shields --------------------------------------------------------------

    public int shieldCreate(int world, int x, int y, int z) {
        try {
            return (int) shieldCreate.invokeExact(world, x, y, z);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public void shieldDestroy(int shield) {
        try {
            shieldDestroy.invokeExact(shield);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean shieldAddTarget(int shield, int x, int y, int z) {
        try {
            return (int) shieldAddTarget.invokeExact(shield, x, y, z) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public boolean shieldRemoveTarget(int shield, int x, int y, int z) {
        try {
            return (int) shieldRemoveTarget.invokeExact(shield, x, y, z) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public void shieldClearTargets(int shield) {
        try {
            shieldClearTargets.invokeExact(shield);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public void shieldSetPowered(int shield, boolean powered) {
        try {
            shieldSetPowered.invokeExact(shield, bit(powered));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public void shieldSetFuel(int shield, int fuel) {
        try {
            shieldSetFuel.invokeExact(shield, fuel);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public ShieldState shieldGetState(int shield) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment powered = arena.allocate(JAVA_INT);
            MemorySegment running = arena.allocate(JAVA_INT);
            MemorySegment fuel = arena.allocate(JAVA_INT);
            shieldGetState.invokeExact(shield, powered, running, fuel);
            return new ShieldState(getInt(powered) != 0, getInt(running) != 0, getInt(fuel));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public Pos shieldGetPos(int shield) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment x = arena.allocate(JAVA_INT);
            MemorySegment y = arena.allocate(JAVA_INT);
            MemorySegment z = arena.allocate(JAVA_INT);
            shieldGetPos.invokeExact(shield, x, y, z);
            return new Pos(getInt(x), getInt(y), getInt(z));
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /**
     * Advances one shield.
     *
     * @param now host monotonic clock in seconds; the library only compares
     *     differences
     * @return points granted this tick
     */
    public int shieldTick(int shield, long now) {
        try {
            // The redundant out param is explicitly optional, and a NULL
            // MemorySegment is address 0, which is what C means by a null
            // pointer. Cheaper than allocating a throwaway slot per tick.
            return (int) shieldTick.invokeExact(shield, now, MemorySegment.NULL);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /** Shield handle at {@code index}, or 0 past the end. */
    public int shieldAt(int world, int index) {
        try {
            return (int) shieldAt.invokeExact(world, index);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    public int shieldCount(int world) {
        try {
            return (int) shieldCount.invokeExact(world);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- persistence ----------------------------------------------------------

    /** Writes the whole world to {@code path} as gzip. */
    public boolean worldSave(int world, String path) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cPath = arena.allocateFrom(path);
            return (int) worldSave.invokeExact(world, cPath) != 0;
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    /**
     * Reads a world back, replacing its contents.
     *
     * @return shields restored, or -1 on failure
     */
    public int worldLoad(int world, String path, int shieldPoolSize, int shieldTargetCapacity) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cPath = arena.allocateFrom(path);
            return (int) worldLoad.invokeExact(world, cPath, shieldPoolSize, shieldTargetCapacity);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    // --- small utilities ------------------------------------------------------

    private static int getInt(MemorySegment segment) {
        return segment.get(JAVA_INT, 0);
    }

    /** Calls a no-argument {@code uint32_t} function, used for the face accessors. */
    private static int callInt(MethodHandle handle) {
        try {
            return (int) handle.invokeExact();
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }

    private static int bit(boolean value) {
        return value ? 1 : 0;
    }
}