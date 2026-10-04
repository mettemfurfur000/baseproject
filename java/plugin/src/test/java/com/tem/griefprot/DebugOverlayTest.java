package com.tem.griefprot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.bukkit.Color;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

class DebugOverlayTest {

    @Test
    @DisplayName("reinforcement color uses red, copper and iron durability anchors")
    void durabilityColorsUseTierAnchors() {
        assertEquals(Color.fromRGB(255, 48, 48), DebugOverlay.durabilityColor(0, 32, 256));
        assertEquals(Color.fromRGB(255, 196, 48), DebugOverlay.durabilityColor(32, 32, 256));
        assertEquals(Color.fromRGB(64, 224, 96), DebugOverlay.durabilityColor(256, 32, 256));
    }

    @Test
    @DisplayName("reinforcement color moves smoothly toward green as durability rises")
    void durabilityColorInterpolatesBetweenAnchors() {
        Color almostSpent = DebugOverlay.durabilityColor(1, 32, 256);
        Color betweenTiers = DebugOverlay.durabilityColor(100, 32, 256);

        assertTrue(almostSpent.getGreen() < DebugOverlay.durabilityColor(32, 32, 256).getGreen());
        assertTrue(betweenTiers.getGreen() > 196);
        assertTrue(betweenTiers.getRed() < 255);
    }

    @Test
    @DisplayName("details overlay reports all face durability values and shield points")
    void detailsReportProtectionAmounts() {
        assertEquals(
                "Block 1,64,-2 | D:0 U:7 N:0 S:3 W:0 E:12 | shield:5",
                DebugOverlay.detailsText(1, 64, -2, new int[] {0, 7, 0, 3, 0, 12}, 5));
    }
}
