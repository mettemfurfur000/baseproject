package com.tem.griefprot;

import com.tem.griefprot.nativebridge.Abi;
import com.tem.griefprot.nativebridge.BreakResult;
import com.tem.griefprot.world.GriefWorld;
import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.format.NamedTextColor;
import org.bukkit.block.Block;
import org.bukkit.block.BlockFace;
import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.EventPriority;
import org.bukkit.event.Listener;
import org.bukkit.event.block.Action;
import org.bukkit.event.block.BlockBreakEvent;
import org.bukkit.event.player.PlayerInteractEvent;
import org.bukkit.event.player.PlayerQuitEvent;

/**
 * Turns a block break into a query against the native store.
 *
 * <p>Reinforcement is per face and {@code BlockBreakEvent} does not say which
 * face was struck, so the face is taken from the click that caused the break when
 * one was remembered. When it was not, the weakest reinforced face is used, since
 * that is the one an attacker could always have aimed at; assuming a stronger one
 * would promise protection the block does not actually have.
 *
 * <p>The handler runs at {@link EventPriority#HIGHEST} and only cancels when the
 * store says the block was protected, so plugins that legitimately override a
 * break still get their say afterwards.
 */
public final class BreakListener implements Listener {

    private final GriefProtPlugin plugin;
    private final BreakFaces faces = new BreakFaces();

    public BreakListener(GriefProtPlugin plugin) {
        this.plugin = plugin;
    }

    @EventHandler(priority = EventPriority.MONITOR, ignoreCancelled = true)
    public void onAim(PlayerInteractEvent event) {
        if (event.getAction() != Action.LEFT_CLICK_BLOCK || event.getClickedBlock() == null) {
            return;
        }
        faces.remember(event.getPlayer().getUniqueId(), event.getClickedBlock(), event.getBlockFace());
    }

    @EventHandler(priority = EventPriority.HIGHEST, ignoreCancelled = true)
    public void onBreak(BlockBreakEvent event) {
        Player player = event.getPlayer();
        Block block = event.getBlock();
        GriefWorld world = plugin.grief(block);
        if (world == null) {
            return;
        }

        int face = resolveFace(world, player, block);
        BreakResult result = world.resolveBreak(block, face, true, true);
        if (!result.ok() || !result.protectedFromBreak()) {
            return;
        }

        event.setCancelled(true);
        player.sendMessage(feedback(result, face));
    }

    /**
     * Works out which face to resolve against.
     *
     * @return the remembered face if there is a trustworthy one, otherwise the
     *     weakest reinforced face, or {@code NONE} if the block is unreinforced
     */
    private int resolveFace(GriefWorld world, Player player, Block block) {
        BlockFace cached = faces.recall(player.getUniqueId(), block);
        if (cached != null) {
            int id = plugin.faces().toAbi(cached);
            if (id != Abi.NO_FACE) {
                return id;
            }
        }
        // Nothing trustworthy was remembered, so fall back to the weakest
        // reinforced face: the one an attacker could always have aimed at.
        int weakest = world.abi().weakestFace(world.handle(), block.getX(), block.getY(), block.getZ());
        return weakest == Abi.NO_FACE ? plugin.abi().faceNone() : weakest;
    }

    private Component feedback(BreakResult result, int face) {
        if (result.reinforcementHeld()) {
            return Component.text("The " + plugin.faces().describe(face) + " face holds.")
                    .color(NamedTextColor.GRAY);
        }
        int left = result.left();
        return Component.text("A shield absorbed that. ")
                .color(NamedTextColor.AQUA)
                .append(Component.text(left + " point" + (left == 1 ? "" : "s") + " left.")
                        .color(NamedTextColor.DARK_AQUA));
    }

    @EventHandler(priority = EventPriority.MONITOR)
    public void onQuit(PlayerQuitEvent event) {
        faces.forget(event.getPlayer().getUniqueId());
    }
}