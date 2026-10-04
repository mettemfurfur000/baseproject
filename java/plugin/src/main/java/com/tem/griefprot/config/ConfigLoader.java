package com.tem.griefprot.config;

import java.util.ArrayList;
import java.util.List;
import org.bukkit.configuration.file.FileConfiguration;

/**
 * Reads {@link PluginConfig} out of a Bukkit config, clamping anything the
 * native library would refuse.
 *
 * <p>Clamping here rather than letting the library do it keeps the failure at
 * startup with a message naming the key, instead of surfacing later as protection
 * that mysteriously stops working.
 */
public final class ConfigLoader {

    private ConfigLoader() {}

    public static PluginConfig load(FileConfiguration config) {
        return new PluginConfig(
                atLeast(config, "max-faces-per-block", 4, 1),
                atLeast(config, "shield-pool", 4096, 1),
                atLeast(config, "shield-regen-seconds", 20, 1),
                atLeast(config, "shield-window-seconds", 300, 1),
                atLeast(config, "shield-range", 4, 0),
                atLeast(config, "shield-max-targets", 64, 1),
                atLeast(config, "shield-fuel-max", 64, 1),
                atLeast(config, "shield-fuel-per-second", 1, 0),
                atLeast(config, "decay-interval-seconds", 300, 1),
                atLeast(config, "decay-amount", 1, 1),
                atLeast(config, "copper-breaks", 32, 1),
                atLeast(config, "iron-breaks", 256, 1),
                atLeast(config, "shield-points-per-window", 1, 1),
                strings(config, "protectable-materials"),
                atLeast(config, "copper-tier-points", 32, 1),
                atLeast(config, "iron-tier-points", 256, 1));
    }

    private static int atLeast(FileConfiguration config, String key, int fallback, int minimum) {
        int value = config.getInt(key, fallback);
        if (value < minimum) {
            return fallback;
        }
        return value;
    }

    private static List<String> strings(FileConfiguration config, String key) {
        if (!config.isList(key)) {
            return List.of();
        }
        List<String> out = new ArrayList<>();
        for (Object raw : config.getList(key)) {
            if (raw instanceof String s && !s.isBlank()) {
                out.add(s);
            }
        }
        return List.copyOf(out);
    }
}