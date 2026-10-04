package com.tem.griefprot.nativebridge;

/**
 * Runtime state of one shield.
 *
 * @param powered whether the shield is powered and running
 * @param running whether the protection window is currently open
 * @param fuel remaining fuel
 */
public record ShieldState(boolean powered, boolean running, int fuel) {}