package com.tem.griefprot.config;

import java.util.List;

/**
 * Tunables for the plugin, read once at startup.
 *
 * <p>The thirteen numeric fields map one for one onto {@code gp_abi_world_config}.
 * They live here rather than being read ad hoc so that a value the library later
 * clamps is caught at startup instead of showing up as mysterious behaviour.
 *
 * @param maxFacesPerBlock how many faces may carry reinforcement at once
 * @param shieldPool how many shields one dimension may hold
 * @param shieldRegen seconds between shield regeneration grants
 * @param shieldWindow seconds a shield protects before it must recharge
 * @param shieldRange radius in blocks a shield reaches
 * @param shieldMaxTargets how many blocks one shield may cover
 * @param shieldFuelMax fuel a full shield holds
 * @param shieldFuelPerSec fuel burned per second while running
 * @param decayInterval seconds before an uncovered record decays
 * @param decayAmount how much one decay sweep removes
 * @param copperBreaks break events the copper tier absorbs
 * @param ironBreaks break events the iron tier absorbs
 * @param shieldPointsPerWindow how many points a covering shield hands out
 * @param protectableMaterials materials players may reinforce, empty for all solid
 * @param copperTierPoints reinforcement value granted by one copper plate
 * @param ironTierPoints reinforcement value granted by one iron plate
 */
public record PluginConfig(
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
        int copperBreaks,
        int ironBreaks,
        int shieldPointsPerWindow,
        List<String> protectableMaterials,
        int copperTierPoints,
        int ironTierPoints) {}