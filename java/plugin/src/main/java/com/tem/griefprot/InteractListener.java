package com.tem.griefprot;

import com.tem.griefprot.world.BukkitWorldPolicy;
import com.tem.griefprot.world.GriefWorld;
import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.format.NamedTextColor;
import org.bukkit.GameMode;
import org.bukkit.Material;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;
import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.block.Action;
import org.bukkit.event.block.BlockPlaceEvent;
import org.bukkit.event.player.PlayerInteractEvent;
import org.bukkit.inventory.EquipmentSlot;

/**
 * Applies reinforcement when a player right-clicks a block face with a reinforcement item.
 *
 * <p>Reinforcement is upgrade only. An item equal to or weaker than what the
 * face already carries is refused, and nothing is spent.
 *
 * <p>The block face comes from {@code PlayerInteractEvent#getBlockFace}, which is
 * the side of the block the player was aiming at, so reinforcing the top of a
 * chest does not reinforce the front.
 */
public final class InteractListener implements Listener {

    private final GriefProtPlugin plugin;

    public InteractListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.NORMAL, ignoreCancelled = true)
    public void onReinforce(PlayerInteractEvent event) {
        if (event.getAction() != Action.RIGHT_CLICK_BLOCK) {
            return;
        }
        // Interact fires once per hand. Only the main hand may spend an item, or
        // a single click would be counted twice.
        if (event.getHand() != EquipmentSlot.HAND) {
            return;
        }

        Block block = event.getClickedBlock();
        if (block == null) {
            return;
        }
        Player player = event.getPlayer();
        Material held = event.getItem().getType();

        int points = tierFor(held);
        if (points == 0) {
            return;
        }

        GriefWorld world = plugin.grief(block);
        if (world == null) {
            return;
        }
        if (!(world.policy() instanceof BukkitWorldPolicy policy) || !policy.isProtectable(block)) {
            return;
        }

        int face = plugin.faces().toAbi(event.getBlockFace());
        if (face == com.tem.griefprot.nativebridge.Abi.NO_FACE) {
            return;
        }

        if (!world.abi().reinfAdd(world.handle(), block.getX(), block.getY(), block.getZ(), face, points)) {
            player.sendMessage(Component.text("That face is already reinforced at least this well.")
                    .color(NamedTextColor.RED));
            return;
        }

        player.sendMessage(Component.text(
                        "Reinforced the " + plugin.faces().describe(face) + " face with "
                                + held.name().toLowerCase(java.util.Locale.ROOT) + ".")
                .color(NamedTextColor.GREEN));

        // Creative players keep the item so the mechanic can be explored
        // without farming.
        if (player.getGameMode() == GameMode.CREATIVE) {
            return;
        }
        consumeOne(player, held);
    }

    /**
     * Removes one reinforcement item.
     *
     * <p>Handled explicitly rather than by cancelling the interaction, because the
     * player is also right-clicking the block and that may have its own meaning.
     */
    private void consumeOne(Player player, Material held) {
        var stack = player.getInventory().getItemInMainHand();
        if (stack.getType() != held) {
            return;
        }
        if (stack.getAmount() <= 1) {
            player.getInventory().setItemInMainHand(null);
        } else {
            stack.setAmount(stack.getAmount() - 1);
            player.getInventory().setItemInMainHand(stack);
        }
    }

    /** Reinforcement value for a tier item, or 0 if it is not a tier item. */
    private int tierFor(Material material) {
        if (material == Material.COPPER_NUGGET) {
            return plugin.config().copperTierPoints();
        }
        if (material == Material.IRON_NUGGET) {
            return plugin.config().ironTierPoints();
        }
        return 0;
    }

    /**
     * Keeps protection from surviving a block that was replaced by a piston or any
     * other means rather than by a player.
     *
     * <p>Without this, a reinforced block that a piston shoves away would keep its
     * record at the old coordinates, protecting whatever moved into them.
     */
    @EventHandler(priority = EventPriority.MONITOR, ignoreCancelled = true)
    public void onPlace(BlockPlaceEvent event) {
        GriefWorld world = plugin.grief(event.getBlock());
        if (world == null) {
            return;
        }
        Block block = event.getBlock();
        if (world.hasProtection(block.getX(), block.getY(), block.getZ())) {
            world.abi().reinfClear(world.handle(), block.getX(), block.getY(), block.getZ());
        }
    }
}