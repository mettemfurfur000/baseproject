package com.tem.griefprot.nativebridge;

/**
 * A snapshot of one world's configuration, as the library actually resolved it.
 *
 * <p>Read this back at startup rather than assuming the defaults: it is the only
 * way to catch a mismatch between what the plugin asked for and what the library
 * clamped it to.
 *
 * @param maxFacesPerBlock how many distinct faces may carry reinforcement
 * @param shieldPool how many shields a world may hold at once
 * @param shieldRegen interval in seconds between regeneration grants
 * @param shieldWindow seconds a shield protects before it must recharge
 * @param shieldRange radius in blocks a shield reaches
 * @param shieldMaxTargets how many blocks one shield may cover
 * @param shieldFuelMax fuel a full shield holds
 * @param shieldFuelPerSec fuel burned per second while running
 * @param decayInterval seconds before an uncovered protection record decays
 * @param decayAmount how much durability one decay sweep removes
 * @param chunkHeight usable world height in blocks
 * @param copperBreaks break events a copper tier absorbs
 * @param ironBreaks break events an iron tier absorbs
 */
public record WorldConfig(
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
        int ironBreaks) {}