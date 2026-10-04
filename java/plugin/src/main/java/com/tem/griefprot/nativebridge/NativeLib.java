package com.tem.griefprot.nativebridge;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.lang.foreign.Arena;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.Linker;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;

/**
 * Loads the compiled {@code griefprot_ffi} shared library and hands out a
 * {@link SymbolLookup} for it.
 *
 * <p>FFM can only load a library from a real file on disk, so the library is
 * shipped inside the jar as a resource and unpacked to a temporary directory on
 * first use. It is loaded into {@link Arena#global()}, which has two
 * consequences worth stating plainly:
 *
 * <ul>
 *   <li>The library stays mapped for the lifetime of the JVM. On Windows that is
 *       mandatory anyway, since a loaded DLL cannot be deleted, so the extracted
 *       copy is intentionally left in place rather than cleaned up on disable.
 *   <li>Lookups and the upcall stubs built from them are not thread confined, so
 *       the C library may invoke a callback from whichever thread called into it.
 *       Callbacks must still be safe to run concurrently; see {@code HostSink}.
 * </ul>
 */
public final class NativeLib implements AutoCloseable {

    /** Name of the library file as produced by {@code make abi}. */
    private static final String BASE = "griefprot";

    private static final NativeLib INSTANCE = new NativeLib();

    private final Path directory;
    private final SymbolLookup lookup;
    private final Linker linker;

    private NativeLib() {
        this.linker = Linker.nativeLinker();
        this.directory = extract();
        this.lookup = SymbolLookup.libraryLookup(directory.resolve(libraryFileName()).toString(), Arena.global());
    }

    /** Loads the library once per JVM. */
    public static NativeLib get() {
        return INSTANCE;
    }

    public Linker linker() {
        return linker;
    }

    public SymbolLookup lookup() {
        return lookup;
    }

    /** Directory the library was unpacked to. Useful for diagnostics. */
    public Path directory() {
        return directory;
    }

    private static String extension() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (os.contains("win")) {
            return ".dll";
        }
        if (os.contains("mac") || os.contains("darwin")) {
            return ".dylib";
        }
        return ".so";
    }

    private static String operatingSystemTag() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (os.contains("win")) {
            return "windows";
        }
        if (os.contains("mac") || os.contains("darwin")) {
            return "macos";
        }
        return "linux";
    }

    /**
     * The platform token used in the packaged resource name, for example
     * {@code _windows_x64.dll}. Windows is spelled out because {@code os.name}
     * varies between "Windows 10" and "Windows 11" across the versions Paper
     * runs on, but the ABI token is fixed.
     */
    public static String platformTag() {
        String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);

        // Paper is x64 in practice, but aarch64 servers exist, and loading a
        // wrong-arch library must fail with a clear message rather than SIGBUS.
        String archTag = switch (arch) {
            case "amd64", "x86_64" -> "x64";
            case "aarch64", "arm64" -> "aarch64";
            default -> arch.replaceAll("[^a-z0-9]", "");
        };
        return "_" + operatingSystemTag() + "_" + archTag + extension();
    }

    private static String libraryFileName() {
        return BASE + platformTag();
    }

    private Path extract() {
        // Prefer the platform specific name; fall back to an untagged one so a
        // single-library jar still works during development.
        String tagged = "/native/" + libraryFileName();
        String generic = "/native/" + BASE + extension();

        try (InputStream in = openResource(tagged, generic)) {
            if (in == null) {
                throw new UnsatisfiedLinkError(
                        "griefprot native library not found in the plugin jar. Looked for "
                                + tagged + " then " + generic
                                + ". Build it with: make -C griefprot abi");
            }
            Path dir = Files.createTempDirectory("griefprot-");
            Path target = dir.resolve(libraryFileName());
            Files.copy(in, target, StandardCopyOption.REPLACE_EXISTING);
            return dir;
        } catch (IOException e) {
            throw new UncheckedIOException("failed to unpack " + libraryFileName(), e);
        }
    }

    private static InputStream openResource(String primary, String fallback) throws IOException {
        InputStream in = NativeLib.class.getResourceAsStream(primary);
        if (in == null) {
            in = NativeLib.class.getResourceAsStream(fallback);
        }
        return in;
    }

    /**
     * Deliberately a no-op. The library is bound to {@link Arena#global()} and,
     * on Windows, cannot be unloaded at all, so there is nothing to release and
     * pretending otherwise would suggest otherwise to the caller.
     */
    @Override
    public void close() {
        // Intentionally empty; see class javadoc.
    }

    @Override
    public String toString() {
        return "griefprot native library (" + libraryFileName() + ") at " + directory;
    }
}