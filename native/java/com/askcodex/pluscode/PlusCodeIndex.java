package com.askcodex.pluscode;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** Immutable V2 data reader. Results use WGS84 and at most 11 significant digits. */
public final class PlusCodeIndex implements AutoCloseable {
    static { System.loadLibrary("pluscode_admin"); }
    private long handle;

    public PlusCodeIndex(String databasePath) throws IOException {
        this(databasePath, 64, 8L * 1024 * 1024);
    }
    public PlusCodeIndex(String databasePath, int cacheCapacity, long cacheBytes) throws IOException {
        if (databasePath == null) throw new IllegalArgumentException("Database path is required");
        handle = nativeOpen(databasePath, cacheCapacity, cacheBytes);
    }
    private long requireOpen() {
        if (handle == 0) throw new IllegalStateException("Index is closed");
        return handle;
    }
    public synchronized Result lookup(String plusCode) throws IOException {
        if (plusCode == null) throw new IllegalArgumentException("Plus Code is required");
        return nativeLookupCode(requireOpen(), plusCode);
    }
    public synchronized Result lookupLatLng(double latitude, double longitude) throws IOException {
        return nativeLookupLatLng(requireOpen(), latitude, longitude);
    }
    public synchronized String metadataJson() throws IOException { return nativeMetadata(requireOpen()); }
    /** Decoded cache allocation bytes and cumulative I/O counters, not total process RSS. */
    public synchronized String cacheStatsJson() throws IOException { return nativeCacheStats(requireOpen()); }
    /** Synchronous best-effort prefetch of a square of 0.05-degree cells. Run off the UI thread. */
    public synchronized String prefetchNearby(double latitude, double longitude, int radiusTiles, int maxTiles) throws IOException {
        return nativePrefetchNearby(requireOpen(), latitude, longitude, radiusTiles, maxTiles);
    }
    public synchronized void clearCache() throws IOException { nativeClearCache(requireOpen()); }
    @Override public synchronized void close() {
        if (handle != 0) {
            long closing = handle;
            handle = 0;
            nativeClose(closing);
        }
    }

    public static final class Admin {
        public final int adminId;
        public final String province, provinceCode, city, cityCode, county, countyCode;
        /** Full dictionary record, including raw fields and source year. */
        public final String json;
        private Admin(int id, String json, String[] fields) {
            this.adminId = id;
            this.json = json;
            province = fields[0]; provinceCode = fields[1];
            city = fields[2]; cityCode = fields[3];
            county = fields[4]; countyCode = fields[5];
        }
    }
    public static final class Result {
        public final String plusCode, status, matchedPlusCode, assignment;
        public final int precision, maxPrecision;
        /** -1 for ambiguous coarse inputs. */
        public final int matchedLength;
        public final boolean boundaryCell, sourceOverlap, includesUncoveredSamples;
        /** Null for ambiguous or outside_coverage results; missing admin fields remain null. */
        public final Admin admin;
        public final List<Admin> sampledCandidates;
        /** Complete response, with exactly the Python V2 JSON semantics. */
        public final String json;
        private Result(String json, String[] text, int[] numbers, boolean[] flags, Admin admin, Admin[] candidates) {
            this.json = json;
            plusCode = text[0]; status = text[1]; matchedPlusCode = text[2]; assignment = text[3];
            precision = numbers[0]; maxPrecision = numbers[1]; matchedLength = numbers[2];
            boundaryCell = flags[0]; sourceOverlap = flags[1]; includesUncoveredSamples = flags[2];
            this.admin = admin;
            sampledCandidates = Collections.unmodifiableList(Arrays.asList(candidates.clone()));
        }
        public boolean isMatched() { return "matched".equals(status); }
        public boolean isAmbiguous() { return "ambiguous".equals(status); }
    }
    private static native long nativeOpen(String path, int capacity, long bytes) throws IOException;
    private static native Result nativeLookupCode(long handle, String code) throws IOException;
    private static native Result nativeLookupLatLng(long handle, double latitude, double longitude) throws IOException;
    private static native String nativeMetadata(long handle) throws IOException;
    private static native String nativeCacheStats(long handle) throws IOException;
    private static native String nativePrefetchNearby(long handle, double latitude, double longitude, int radiusTiles, int maxTiles) throws IOException;
    private static native void nativeClearCache(long handle) throws IOException;
    private static native void nativeClose(long handle);
}
