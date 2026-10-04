# griefprot

Portable C backend for block reinforcement and shield protection, meant to be
embedded by a Minecraft plugin or mod (and other games later) rather than run on
its own. No Minecraft types cross this boundary: the library only knows about
positions and numbers, and the host supplies the callbacks.

## What it does

**Side reinforcement.** A consumable applied to one face of a block sets that
face's break allowance. Copper nugget 32, iron 256, both configurable. A block
holds at most four reinforced faces out of six, so a solid wall can be fully
covered while a protruding block always keeps a weak spot. Reinforcement does
not decay.

The allowance is a tier value, not an increment, so nothing stacks: applying a
second plate to a reinforced face either upgrades it in place, when the new plate
is strictly stronger, or is refused. Two copper plates never become 64, and a
copper plate never weakens an iron one. A refusal leaves the block untouched and
returns false, which is a host's signal to keep the item it was about to spend.

Note that `gp_chunk_reinf_clear` clears a whole block rather than a single face,
so upgrade-in-place is the only way to move a face between tiers without losing
the block's other faces.

**Shield points.** A separate number from reinforcement, covering the whole
block rather than individual faces. A shield distributes its pool evenly across
its targets, regenerating one point at a time in a round robin so the budget is
shared fairly rather than drained into whichever target comes first. With no
targets selected it covers a cube of `(2r+1)^3` blocks instead. Shield points
decay once no live shield is covering them.

## Coverage and decay

Decay only touches blocks no live shield is holding up. A running shield marks
the blocks it is responsible for once per regeneration window, and the chunk's
sweep skips anything still marked. Without this a covered block would lose points
every sweep and be handed them back on the next window, which shows up as a
visible flicker in durability.

The mark is a deadline, not a reference count. Shields unload, get broken, run
out of fuel, and disappear, and none of those events tell the blocks they were
covering, so a claim that had to be released explicitly would outlive its shield
and freeze decay forever. Instead each claim expires one decay interval after the
last window that refreshed it: a shield that stops running simply lets its
coverage lapse, and the blocks start bleeding again. A claim never shortens a
longer one, so a slow shield cannot hand a block back to decay while a fast one
still holds it.

Marking only ever protects points that already exist. Covering a block with no
points allocates nothing, which matters because a range shield considers its
entire cube every window.

Coverage is claimed over the whole eligible set rather than alongside the grants,
because the grant pass stops as soon as the window's budget is spent and does
nothing at all once every target is topped up. Tying the two together would leave
fully protected blocks unmarked and let decay bleed them.

Two limits worth knowing:

- A block whose coverage has lapsed can still be saved by a grant landing in the
  same tick as the sweep, depending on the host's ordering of the tick and the
  chunk sweep. The library does not sequence them for you.
- A block's coverage is only as fresh as the last window to claim it. A shield
  with more targets than its decay interval can leave the tail of its own list
  briefly uncovered. Raise the interval rather than the target count if that
  shows.

Breaking a block resolves in a fixed order: a reinforced face absorbs the break
first, and only an unprotected face falls through to shield points. So a block
can have reinforcement, shield points, both, or neither.

Explosions use that same resolution for each affected block. The face nearest
the explosion origin is checked first; reinforcement absorbs the hit before
shield points. A protected block is removed from the explosion's affected list,
so the explosion can still damage other blocks normally. When a ray to a block
first hits another block that survived the explosion check, the target is
removed as well.

## Debug overlays

Operators can inspect the block under their crosshair (up to 8 blocks away) with
`/griefprot overlay reinforcement`. Protected faces are drawn as particle
outlines; their color moves from red at zero durability through the copper tier
to green at the iron tier. Use `/griefprot overlay shield` for a separate blue
wireframe when the target has shield points, or `/griefprot overlay off` to stop.
Only one overlay can be active per player at a time.

## Storage

Reinforcement is sparse per chunk. Chunks are `16 x chunk_height x 16` (384 in
the overworld, 128 in the nether), and only blocks that actually carry
protection occupy a slot, so a chunk costs what its reinforced blocks cost and
not what its volume costs.

A reinforcement record is 10 bytes: four `u16` durabilities plus four 3-bit face
ids packed into a `u16`.

```
config: copper=32 iron=256 pool=4096 regen=20 range=4 decay=1/300s
  100 reinforced blocks: 3712 bytes sparse vs 983040 dense (264.8x)
all tests passed
```

Shield points live in the chunk that owns the block, not in the shield's block
entity. A block therefore never has to load its shielding shield's chunk to read
its own protection; the cost is that it bleeds shield points while that shield
sits in an unloaded chunk.

## Build

```
make             # build/libgriefprot.a and build/test.exe
make test        # same, then run the suite
make abi         # build/griefprot_ffi.dll, the FFM shim for the Java host
make abi-test    # build and run the shim's own tests
make abi-exports # assert every header function is reachable by name
make clean
```

Uses `temlib` from the sibling directory, following its layout: `src/`,
`include/`, `mains/`, one static lib plus one target per file in `mains/`.

## Plugin

The Paper plugin lives in `java/plugin` and talks to `gp_abi.h` through Java 25
FFM. Build it with:

```
java/plugin/scripts/build.sh package    # runs `make abi`, stages the library, clean-packages
```

Maven rather than Gradle, deliberately. Gradle 8.13 cannot run on a Java 25 JVM
at all: its embedded Kotlin `JavaVersion.parse` throws on `25.0.2` before the
build starts. A Java 25 toolchain declaration needs Gradle 9.1+, and javac from an
older JDK cannot target 25 either, so no Gradle version available here can compile
it. `scripts/build.sh` sets `JAVA_HOME` to a Java 25 JDK if it is not already set
and runs Maven's `clean` goal before the requested goals, avoiding stale compiled
classes in the packaged plugin.

The shared library is shipped inside the jar as a resource and unpacked at
startup, because FFM can only load a library from a real file on disk. It is
loaded into `Arena.global()`, so it is never unloaded and the unpacked copy is
left in place; on Windows a loaded DLL cannot be deleted anyway.

`api-version` is `26.1.2`, matching the server's `currentApiVersion`.

## Layout

| file | role |
|---|---|
| `include/griefprot.h` | positions, tunables, reinforcement tiers |
| `include/griefprot_sparse.h` | open-addressing u32 -> fixed-record map, the storage primitive |
| `include/griefprot_chunk.h` | per-chunk reinforcement, shield points, break resolution, decay |
| `include/griefprot_shield.h` | shield entity, target list, round robin regeneration |
| `include/griefprot_world.h` | chunk registry keyed by chunk coords, shield registry |
| `include/griefprot_serialize.h` | zlib/gzip TKV pack and unpack, per chunk and whole world |
| `mains/test.c` | test suite, 32 groups |
| `java/native/gp_abi.h` | the flat FFM-facing surface, and its only supported entry points |
| `java/native/gp_abi.c` | shim implementation: handles, coordinate mapping, callback bridge |
| `java/native/gp_abi_test.c` | shim contract tests, 184 checks |
| `java/plugin/src/main/java/com/tem/griefprot/nativebridge/Abi.java` | every downcall, typed; the whole Java side of the C boundary |
| `java/plugin/src/main/java/com/tem/griefprot/HostSink.java` | the three host callbacks as FFM upcall stubs |
| `java/plugin/src/main/java/com/tem/griefprot/world/GriefWorld.java` | one native world per dimension, chunk residency, persistence |
| `java/plugin/src/test/java/com/tem/griefprot/nativebridge/AbiTest.java` | 29 checks driving the real compiled library through FFM |

## Java host boundary

A JVM cannot call C structs by value without pinning down their layout, and a
layout that is wrong on one compiler is silently wrong rather than a compile
error. So Java never sees `gp_pos`, `gp_config`, `gp_break_result` or
`gp_shield_sink`. It sees only:

- `gp_h_world` and `gp_h_shield`, which are `uint32_t` handles, 0 meaning invalid
- scalars, by value
- callback addresses, which Java turns into upcall stubs
- out-parameters, which are pointers to Java-allocated `MemorySegment`

The shim owns every translation: world position to chunk plus local
coordinates, handle to object, and the sink callbacks from the library's
`gp_pos`-by-value form to the host's flat form. Chunk level calls take local
coordinates, so anything that forgets the conversion is wrong outside chunk 0
rather than crashing, which is why the shim resolves the chunk and its locals
together in one place.

`gp_abi_check()` verifies the assumptions the Java side hardcodes (pointer
width, `u32`/`u64` width, `gp_pos` field order, the face enum values) and returns
a bitmask, so a toolchain mismatch fails at startup instead of corrupting state
later. Call it before anything else.

Two conventions the host depends on:

- Every entry point tolerates handle 0 and returns a defined value rather than
  faulting, and always writes its out-parameters, so a stale handle in Java
  cannot take the server down or make a decision from uninitialised memory.
- `gp_abi_resolve_break` on an unloaded chunk reports "nothing protected this"
  and does not load the chunk. Resolving a break never allocates.

## Persistence

Serialization writes TKV trees through a `stream_t`, so passing a compressed
stream to `stream_open_write` gets gzip for free and a plain stream gets raw
bytes.

Per chunk:

```c
stream_t out;
stream_open_write("region/0.0.gz", 1, &out);
gp_chunk_write_stream(chunk, &out);
stream_close(&out);
```

and back with `gp_chunk_read_stream`.

Whole world:

```c
gp_world_write_stream(world, &out);
```

which writes a small metadata object followed by length prefixed per chunk and
per shield trees. Reading needs the metadata object first, then the stream:

```c
tkv_object header = tkv_read_from_stream(&in, scratch, meta);
i32 n = gp_world_unpack(header, &in, world, shield_pool, pool_size, target_cap, scratch);
```

Three constraints shape the format:

- `tkv_object_add_field` serializes through an 8192 byte stack buffer, so no
  single value can exceed that. Chunk records are therefore split across
  numbered array groups (`r0`, `r1`, ...) of `GP_RECORDS_PER_GROUP` records.
- `add_field` copies the object and returns a new pointer, so packing code must
  rebind the object on every field.
- TKV converts endianness for scalars via `stream_read`, but array payloads are
  copied verbatim. Every multi byte value inside a record is encoded little
  endian by hand so a save is portable between hosts.

Record layouts are versioned by `GP_FORMAT_VERSION`; a mismatch is refused
rather than misread, because silently loading an empty chunk would wipe
protection data.

The current version is 2. Version 1 predates coverage claims: its per-block
shield record was 8 bytes and now carries a 12 byte record with a coverage
expiry. Version 1 files are rejected rather than migrated, since a coverage
claim cannot be reconstructed from data written before coverage existed, and
treating those blocks as uncovered would bleed every shield the first time decay
ran. A host upgrading from version 1 should discard its saves rather than expect
them to load.

## Block movement

`gp_world_move_block(world, from, to, motion)` handles piston and dropper moves,
including across a chunk border. `motion` is the direction the block travels; the
side it was pushed or pulled from is the opposite face either way, since a block
shoved east came off its west side and one dragged west came off its east side.

It returns false, changing nothing, when:

- `motion` is not a real face, or either position lies outside the world
- the side being moved off currently carries reinforcement
- the destination's faces plus the incoming ones would exceed
  `cfg.max_faces_per_block`

On success reinforcement keeps its world space direction, since a piston
translates a block without rotating it, durability on a shared face is summed,
and shield points travel with the block. A refused move leaves both positions
untouched, so the host can cancel the piston action instead of silently losing
protection.

`gp_chunk_reinf_move` and `gp_chunk_reinf_transfer` / `gp_chunk_shield_transfer`
are the chunk-level primitives. Each transfer validates coordinates against the
chunk they belong to, so a cross chunk caller cannot address a local cell in the
source chunk by mistake.

## Host responsibilities

The library is deliberately ignorant of block types. The host decides what may
be protected or reinforced by answering two questions through `gp_shield_sink`:
whether a position is protectable, and granting points up to a cap. That covers
"instabreakable" blocks like torches and grass, and excluding other shields from
coverage, without the library carrying a block table.

A host also needs to route block move events through `gp_world_move_block`, and
to route `gp_world` chunks to its own chunk load and unload lifecycle.

Wiring decay up means setting one field on the sink:

```c
gp_shield_sink sink = {0};
sink.protectable = host_protectable;
sink.grant       = host_grant;
sink.cover       = gp_world_shield_cover;  // user = world
```

Zero the struct: the optional callbacks must be NULL when unused, and filling the
required fields individually leaves the rest as stack garbage.

### Shield clocks

`gp_shield_tick(shield, now, ...)` takes a host monotonic clock in whatever unit
the host likes; only differences matter. A new shield anchors `window_start` on
its first tick and grants nothing until a full window has passed. Without that
anchor a shield would treat the whole span between the epoch and its first tick
as elapsed and burn its entire fuel reserve at once, which is exactly what a host
passing wall-clock milliseconds would trigger.

A shield restored from disk is also unanchored, so it re-anchors rather than
charging fuel for the time it spent unloaded.

## Still to build

- the Paper plugin: FFM bindings, per dimension worlds, break, interaction and
  explosion listeners, chunk and piston routing
- a mod adapter once the plugin side is proven
