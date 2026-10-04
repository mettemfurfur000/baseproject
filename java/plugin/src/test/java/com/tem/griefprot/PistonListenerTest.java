package com.tem.griefprot;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.bukkit.block.BlockFace;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

class PistonListenerTest {

    @Test
    @DisplayName("retractions guard the face matching the pulled block's travel direction")
    void retractGuardsTravelFacingSide() {
        assertEquals(BlockFace.WEST, PistonListener.sourceFaceToGuard(BlockFace.WEST, true));
    }

    @Test
    @DisplayName("extensions guard the face opposite the pushed block's travel direction")
    void extendGuardsOppositeTravelSide() {
        assertEquals(BlockFace.WEST, PistonListener.sourceFaceToGuard(BlockFace.EAST, false));
    }
}
