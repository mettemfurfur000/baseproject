package com.tem.griefprot.nativebridge;

/**
 * Out parameters of {@code gp_abi_resolve_break}.
 *
 * <p>{@code ok} matters as much as the numbers. The shim refuses a call for a
 * bad handle or a face id it does not recognise, and every out param is then left
 * zeroed, which reads as "reinforcement held the block" if taken at face value.
 * A host that treated that as protection would make the whole world unbreakable,
 * so the refusal is carried explicitly instead of being inferred from zeros.
 *
 * @param ok whether the library answered at all
 * @param reinfSurvived 1 if a reinforced face absorbed the break
 * @param blockBroke 1 if the block itself was destroyed
 * @param absorbed shield points spent
 * @param left shield points remaining on the block
 */
public record BreakResult(boolean ok, int reinfSurvived, int blockBroke, int absorbed, int left) {

    /**
     * The refusal case: nothing is known, and nothing is protected. {@code
     * blockBroke} is 1 rather than 0 so that {@link #destroyed()} cannot be
     * fooled by the zeroed out params either.
     */
    public static final BreakResult FAILED = new BreakResult(false, 0, 1, 0, 0);

    /**
     * Nothing stood between the player and the break, so the caller should let it
     * happen.
     */
    public boolean destroyed() {
        return ok && blockBroke != 0;
    }

    /**
     * The block is still standing. Either a reinforced face held it, or shield
     * points paid for it; see {@link #reinforcementHeld()} to tell which.
     */
    public boolean protectedFromBreak() {
        return ok && blockBroke == 0;
    }

    /** A reinforced face absorbed the break, so shield points were left alone. */
    public boolean reinforcementHeld() {
        return ok && reinfSurvived != 0;
    }

    /** Shield points paid for the break instead. */
    public boolean shieldPaid() {
        return ok && reinfSurvived == 0 && blockBroke == 0 && absorbed != 0;
    }
}