package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import com.tem.griefprot.world.WorldPolicy;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Level;
import java.util.logging.Logger;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

/**
 * The three functions the C library calls back into, as FFM upcall stubs.
 *
 * <p>The library decides what it may protect by asking the host, because only
 * the host knows what a Minecraft block is. Those questions arrive as raw C
 * function pointers, so this class turns three static Java methods into
 * trampolines and hands their addresses to the library.
 *
 * <p>Two constraints shape everything here:
 *
 * <ul>
 *   <li><b>No exceptions may escape.</b> A throw across an upcall boundary is
 *       undefined behaviour in C, so every entry point swallows {@code
 *       Throwable} and returns the value that makes the library do nothing
 *       harmful.
 *   <li><b>{@code host} carries the world.</b> One set of stubs serves every
 *       dimension; the library passes back the id it was configured with, which
 *       is how a callback finds the Bukkit world it belongs to.
 * </ul>
 */
public final class HostSink {

    private static final Logger LOG = Logger.getLogger("griefprot");

    private static final Map<Long, GriefWorld> BY_HOST = new ConcurrentHashMap<>();

    private static final long PROTECTABLE_STUB;
    private static final long GRANT_STUB;
    private static final long COVER_STUB;

    static {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        Arena arena = Arena.global();
        var linker = com.tem.griefprot.nativebridge.NativeLib.get().linker();

        PROTECTABLE_STUB = stub(linker, arena, lookup, "protectable",
                MethodType.methodType(int.class, long.class, int.class, int.class, int.class),
                FunctionDescriptor.of(JAVA_INT, JAVA_LONG, JAVA_INT, JAVA_INT, JAVA_INT));

        GRANT_STUB = stub(linker, arena, lookup, "grant",
                MethodType.methodType(int.class, long.class, int.class, int.class, int.class, int.class, int.class),
                FunctionDescriptor.of(JAVA_INT, JAVA_LONG, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));

        COVER_STUB = stub(linker, arena, lookup, "cover",
                MethodType.methodType(void.class, long.class, int.class, int.class, int.class, long.class),
                FunctionDescriptor.ofVoid(JAVA_LONG, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG));
    }

    private HostSink() {}

    private static long stub(
            java.lang.foreign.Linker linker,
            Arena arena,
            MethodHandles.Lookup lookup,
            String name,
            MethodType type,
            FunctionDescriptor descriptor) {
        try {
            MethodHandle target = lookup.findStatic(HostSink.class, name, type);
            return linker.upcallStub(target, descriptor, arena).address();
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** Forces class initialisation, so the stubs exist before any world is built. */
    public static void ensureInitialised() {
        // Intentionally empty; the static initialiser does the work.
    }

    public static long protectableStub() {
        return PROTECTABLE_STUB;
    }

    public static long grantStub() {
        return GRANT_STUB;
    }

    public static long coverStub() {
        return COVER_STUB;
    }

    /** Publishes a world under the id the library will hand back to callbacks. */
    public static void register(long hostId, GriefWorld world) {
        BY_HOST.put(hostId, world);
    }

    /** Withdraws a world. Called before its native world is destroyed. */
    public static void unregister(long hostId) {
        BY_HOST.remove(hostId);
    }

    /**
     * Called from C: may this block carry protection at all?
     *
     * <p>An unknown or unregistered host answers 0. That makes the library skip
     * the block, which is the safe direction: a protection it thinks exists but
     * the host cannot service is worse than one it never records.
     */
    private static int protectable(long host, int x, int y, int z) {
        try {
            GriefWorld world = BY_HOST.get(host);
            if (world == null) {
                return 0;
            }
            WorldPolicy policy = world.policy();
            return policy.protectable(world, x, y, z) ? 1 : 0;
        } catch (Throwable t) {
            LOG.log(Level.SEVERE, "protectable callback failed for " + host + " at " + x + "," + y + "," + z, t);
            return 0;
        }
    }

    /**
     * Called from C: hand out shield points, capped at {@code cap}.
     *
     * <p>Returning 0 is always safe. The library treats the grant as refused and
     * simply leaves the block unprotected this window.
     */
    private static int grant(long host, int x, int y, int z, int amount, int cap) {
        try {
            GriefWorld world = BY_HOST.get(host);
            if (world == null) {
                return 0;
            }
            return world.policy().grant(world, x, y, z, amount, cap);
        } catch (Throwable t) {
            LOG.log(Level.SEVERE, "grant callback failed for " + host + " at " + x + "," + y + "," + z, t);
            return 0;
        }
    }

    /** Called from C: a shield is covering this block right now. */
    private static void cover(long host, int x, int y, int z, long now) {
        try {
            GriefWorld world = BY_HOST.get(host);
            if (world != null) {
                world.policy().cover(world, x, y, z, now);
            }
        } catch (Throwable t) {
            LOG.log(Level.SEVERE, "cover callback failed for " + host + " at " + x + "," + y + "," + z, t);
        }
    }
}