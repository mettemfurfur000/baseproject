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
}
