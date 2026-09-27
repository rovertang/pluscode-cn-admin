import com.askcodex.pluscode.PlusCodeIndex;
import java.util.concurrent.*;

public final class JniSmoke {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        PlusCodeIndex index = new PlusCodeIndex(args[0], 8, 8 * 1024 * 1024);
        check(index.lookup("8PFRWC34+MX2").admin.countyCode.equals("110101"));
        check(index.lookupLatLng(39.9042, 116.4074).admin.province.equals("\u5317\u4eac\u5e02"));
        PlusCodeIndex.Result coarse = index.lookup("8P67C2C9+GP");
        check(coarse.isAmbiguous() && coarse.admin == null && coarse.matchedLength == -1);
        check(!coarse.sampledCandidates.isEmpty());
        check(index.lookup("8P67C2C9+GP422").precision == 11);
        check(index.lookupLatLng(22.3193, 114.1694).admin.county == null);
        check(index.lookupLatLng(48.8566, 2.3522).status.equals("outside_coverage"));
        check(index.metadataJson().contains("olc-adaptive-4-6-8-10-11-v2"));
        try { index.lookup("BAD"); throw new AssertionError(); } catch (IllegalArgumentException expected) {}
        try { index.lookup("8PFRWC34+MX2\0junk"); throw new AssertionError(); } catch (IllegalArgumentException expected) {}
        try { index.lookupLatLng(Double.NaN, 0); throw new AssertionError(); } catch (IllegalArgumentException expected) {}
        ExecutorService pool = Executors.newFixedThreadPool(4);
        try {
            java.util.List<Future<?>> futures = new java.util.ArrayList<>();
            for (int t = 0; t < 4; ++t) futures.add(pool.submit(() -> {
                for (int n = 0; n < 250; ++n) try { check(index.lookup("8P67C2C9+GP4").isMatched()); }
                catch (Exception error) { throw new RuntimeException(error); }
            }));
            for (Future<?> future : futures) future.get();
        } finally { pool.shutdown(); }
        check(index.cacheStatsJson().contains("cached_bytes"));
        check(index.prefetchNearby(39.9, 116.4, 1, 9).contains("retained_tiles"));
        try { index.prefetchNearby(0, 0, -1, 9); throw new AssertionError(); } catch (IllegalArgumentException expected) {}
        index.clearCache();
        index.close(); index.close();
        try { index.lookup("8PFRWC34+MX2"); throw new AssertionError(); } catch (IllegalStateException expected) {}
        try (PlusCodeIndex fixture = new PlusCodeIndex(args[1])) {
            check(fixture.lookup("6FG22223+").admin.province.equals("Province \uD83D\uDE00"));
        }
        try (PlusCodeIndex missing = new PlusCodeIndex(args[0] + ".missing")) { throw new AssertionError(); }
        catch (java.io.IOException expected) {}
        System.out.println("JNI typed results, UTF-8, errors, close and 1000 concurrent lookups passed");
    }
}
