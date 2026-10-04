package com.tem.griefprot;

import com.tem.griefprot.world.GriefWorld;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.bukkit.Color;
import org.bukkit.Particle;
import org.bukkit.Particle.DustOptions;
import net.kyori.adventure.text.Component;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;
import org.bukkit.command.Command;
import org.bukkit.command.CommandExecutor;
import org.bukkit.command.CommandSender;
import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.Listener;
import org.bukkit.event.player.PlayerQuitEvent;
import org.bukkit.scheduler.BukkitRunnable;

/** Opt-in, crosshair-targeted particle overlays for testers. */
public final class DebugOverlay extends BukkitRunnable implements CommandExecutor, Listener {

    private static final List<BlockFace> FACES =
            List.of(BlockFace.DOWN, BlockFace.UP, BlockFace.NORTH, BlockFace.SOUTH, BlockFace.WEST, BlockFace.EAST);
    private static final int LINE_SEGMENTS = 4;
    private static final Color EMPTY_COLOR = Color.fromRGB(255, 48, 48);
    private static final Color COPPER_COLOR = Color.fromRGB(255, 196, 48);
    private static final Color IRON_COLOR = Color.fromRGB(64, 224, 96);
    private static final Color SHIELD_COLOR = Color.fromRGB(64, 192, 255);

    private final GriefProtPlugin plugin;
    private final Map<UUID, Mode> enabled = new HashMap<>();

    public DebugOverlay(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @Override
    public boolean onCommand(CommandSender sender, Command command, String label, String[] args) {
        if (!(sender instanceof Player player)) {
            sender.sendMessage("Debug overlays are only available to players.");
            return true;
        }
        if (args.length != 2 || !args[0].equalsIgnoreCase("overlay")) {
            return false;
        }

        Mode requested = switch (args[1].toLowerCase(java.util.Locale.ROOT)) {
            case "reinforcement" -> Mode.REINFORCEMENT;
            case "shield" -> Mode.SHIELD;
            case "details" -> Mode.DETAILS;
            case "off" -> null;
            default -> null;
        };
        if (requested == null && !args[1].equalsIgnoreCase("off")) {
            player.sendMessage("Usage: /griefprot overlay <reinforcement|shield|details|off>");
            return true;
        }

        if (requested == null || enabled.get(player.getUniqueId()) == requested) {
            enabled.remove(player.getUniqueId());
            player.sendMessage("Protection overlay disabled.");
            return true;
        }

        enabled.put(player.getUniqueId(), requested);
        player.sendMessage("Showing " + requested.name().toLowerCase(java.util.Locale.ROOT) + " overlay "
                + "for the block you are looking at. Use /griefprot overlay off to stop.");
        return true;
    }

    @Override
    public void run() {
        for (Player player : plugin.getServer().getOnlinePlayers()) {
            if (player == null) {
                continue;
            }
            Mode mode = enabled.get(player.getUniqueId());
            if (mode == null) {
                continue;
            }

            Block block = player.getTargetBlockExact(8);
            if (block == null) {
                if (mode == Mode.DETAILS) {
                    player.sendActionBar(Component.empty());
                }
                continue;
            }
            GriefWorld world = plugin.grief(block);
            if (world == null) {
                if (mode == Mode.DETAILS) {
                    player.sendActionBar(Component.text("World is not tracked by GriefProt."));
                }
                continue;
            }

            if (mode == Mode.REINFORCEMENT) {
                showReinforcement(player, block, world);
            } else if (mode == Mode.SHIELD) {
                showShield(player, block, world);
            } else {
                showDetails(player, block, world);
            }
        }
    }

    private void showDetails(Player player, Block block, GriefWorld world) {
        int[] durability = new int[FACES.size()];
        for (int i = 0; i < FACES.size(); i++) {
            int face = plugin.faces().toAbi(FACES.get(i));
            durability[i] = world.abi().reinfFaceDurability(
                    world.handle(), block.getX(), block.getY(), block.getZ(), face);
        }
        int shield = world.abi().pointsGet(world.handle(), block.getX(), block.getY(), block.getZ());
        player.sendActionBar(Component.text(detailsText(block.getX(), block.getY(), block.getZ(), durability, shield)));
    }

    static String detailsText(int x, int y, int z, int[] durability, int shieldPoints) {
        if (durability.length != FACES.size()) {
            throw new IllegalArgumentException("expected durability for all six block faces");
        }
        List<String> entries = new ArrayList<>(FACES.size());
        for (int i = 0; i < FACES.size(); i++) {
            entries.add(faceShortName(FACES.get(i)) + ":" + durability[i]);
        }
        return "Block " + x + "," + y + "," + z + " | " + String.join(" ", entries)
                + " | shield:" + shieldPoints;
    }

    private static String faceShortName(BlockFace face) {
        return switch (face) {
            case DOWN -> "D";
            case UP -> "U";
            case NORTH -> "N";
            case SOUTH -> "S";
            case WEST -> "W";
            case EAST -> "E";
            default -> throw new IllegalArgumentException("not a block face: " + face);
        };
    }

    private void showReinforcement(Player player, Block block, GriefWorld world) {
        for (BlockFace face : FACES) {
            int id = plugin.faces().toAbi(face);
            int durability = world.abi().reinfFaceDurability(
                    world.handle(), block.getX(), block.getY(), block.getZ(), id);
            if (durability > 0) {
                drawFace(player, block, face, durabilityColor(
                        durability, plugin.config().copperTierPoints(), plugin.config().ironTierPoints()));
            }
        }
    }

    private void showShield(Player player, Block block, GriefWorld world) {
        if (world.abi().pointsGet(world.handle(), block.getX(), block.getY(), block.getZ()) > 0) {
            drawCube(player, block, SHIELD_COLOR);
        }
    }

    private void drawFace(Player player, Block block, BlockFace face, Color color) {
        double x = block.getX();
        double y = block.getY();
        double z = block.getZ();
        double[][] corners = switch (face) {
            case DOWN -> new double[][] {{x, y, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x, y, z + 1}};
            case UP -> new double[][] {{x, y + 1, z}, {x + 1, y + 1, z}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}};
            case NORTH -> new double[][] {{x, y, z}, {x + 1, y, z}, {x + 1, y + 1, z}, {x, y + 1, z}};
            case SOUTH -> new double[][] {
                {x, y, z + 1}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}
            };
            case WEST -> new double[][] {{x, y, z}, {x, y, z + 1}, {x, y + 1, z + 1}, {x, y + 1, z}};
            case EAST -> new double[][] {
                {x + 1, y, z}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}, {x + 1, y + 1, z}
            };
            default -> throw new IllegalArgumentException("not a block face: " + face);
        };
        DustOptions dust = new DustOptions(color, 0.8f);
        for (int i = 0; i < corners.length; i++) {
            drawLine(player, corners[i], corners[(i + 1) % corners.length], dust);
        }
    }

    private void drawCube(Player player, Block block, Color color) {
        double x = block.getX();
        double y = block.getY();
        double z = block.getZ();
        double[][] corners = {
            {x, y, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x, y, z + 1},
            {x, y + 1, z}, {x + 1, y + 1, z}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}
        };
        int[][] edges = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };
        DustOptions dust = new DustOptions(color, 1.0f);
        for (int[] edge : edges) {
            drawLine(player, corners[edge[0]], corners[edge[1]], dust);
        }
    }

    private void drawLine(Player player, double[] start, double[] end, DustOptions dust) {
        for (int step = 0; step < LINE_SEGMENTS; step++) {
            double t = (double) step / LINE_SEGMENTS;
            player.spawnParticle(
                    Particle.DUST,
                    start[0] + (end[0] - start[0]) * t,
                    start[1] + (end[1] - start[1]) * t,
                    start[2] + (end[2] - start[2]) * t,
                    1,
                    0,
                    0,
                    0,
                    0,
                    dust);
        }
    }

    static Color durabilityColor(int durability, int copperTier, int ironTier) {
        if (durability <= 0) {
            return EMPTY_COLOR;
        }

        int lowerTier = Math.min(copperTier, ironTier);
        int upperTier = Math.max(copperTier, ironTier);
        if (durability <= lowerTier) {
            return blend(EMPTY_COLOR, COPPER_COLOR, (double) durability / lowerTier);
        }
        if (durability < upperTier) {
            return blend(COPPER_COLOR, IRON_COLOR, (double) (durability - lowerTier) / (upperTier - lowerTier));
        }
        return IRON_COLOR;
    }

    private static Color blend(Color start, Color end, double amount) {
        double t = Math.max(0.0, Math.min(1.0, amount));
        return Color.fromRGB(
                (int) Math.round(start.getRed() + (end.getRed() - start.getRed()) * t),
                (int) Math.round(start.getGreen() + (end.getGreen() - start.getGreen()) * t),
                (int) Math.round(start.getBlue() + (end.getBlue() - start.getBlue()) * t));
    }

    @EventHandler
    public void onQuit(PlayerQuitEvent event) {
        enabled.remove(event.getPlayer().getUniqueId());
    }

    private enum Mode {
        REINFORCEMENT,
        SHIELD,
        DETAILS
    }

}
