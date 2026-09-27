import com.askcodex.pluscode.PlusCodeIndex;

public final class JavaExample {
    public static void main(String[] args) throws Exception {
        try (PlusCodeIndex index = new PlusCodeIndex(args[0])) {
            PlusCodeIndex.Result result = index.lookupLatLng(39.9042, 116.4074);
            System.out.println(result.json);
            System.out.println(index.cacheStatsJson());
        }
    }
}
