package com.tem.griefprot;

import com.tem.griefprot.nativebridge.Abi;
import java.util.EnumMap;
import java.util.HashMap;
import java.util.Map;
import org.bukkit.block.BlockFace;

/**
 * Translates between Bukkit's {@link BlockFace} and the library's face ids.
 *
 * <p>The ids are read from the library at startup rather than assumed, so a
 * change to the C enum cannot quietly make the Bukkit mapping point at the wrong
 * side of a block.
 *
 * <p>This lives outside {@code nativebridge} on purpose: that package models the
 * C boundary and knows nothing about Bukkit, which keeps it testable without a
 * server.
 */
public final class Faces {

    private final Abi abi;
    private final Map<BlockFace, Integer> toAbi = new EnumMap<>(BlockFace.class);
    private final Map<Integer, BlockFace> toBukkit = new HashMap<>();

    public Faces(Abi abi) {
        this.abi = abi;

        toAbi.put(BlockFace.DOWN, abi.faceDown());
        toAbi.put(BlockFace.UP, abi.faceUp());
        toAbi.put(BlockFace.NORTH, abi.faceNorth());
        toAbi.put(BlockFace.SOUTH, abi.faceSouth());
        toAbi.put(BlockFace.WEST, abi.faceWest());
        toAbi.put(BlockFace.EAST, abi.faceEast());

        for (Map.Entry<BlockFace, Integer> entry : toAbi.entrySet()) {
            BlockFace previous = toBukkit.put(entry.getValue(), entry.getKey());
            if (previous != null) {
                throw new IllegalStateException(
                        "the library reported face id " + entry.getValue() + " for both " + previous
                                + " and " + entry.getKey());
            }
        }
    }

    /**
     * The library's id for a Bukkit face, or {@link Abi#NO_FACE} for the two
     * faces that are not block faces.
     */
    public int toAbi(BlockFace face) {
        Integer id = face == null ? null : toAbi.get(face);
        return id == null ? Abi.NO_FACE : id;
    }

    /** The Bukkit face for a library id, or {@link BlockFace#SELF} if there is none. */
    public BlockFace toBukkit(int id) {
        BlockFace face = toBukkit.get(id);
        return face == null ? BlockFace.SELF : face;
    }

    /** The face on the other side of a block, as the library would number it. */
    public int opposite(BlockFace face) {
        return toAbi(face.getOppositeFace());
    }

    /** Exposed so callers can build a message naming the face that held. */
    public String describe(int id) {
        return toBukkit(id).name().toLowerCase(java.util.Locale.ROOT);
    }

    Abi abi() {
        return abi;
    }
}