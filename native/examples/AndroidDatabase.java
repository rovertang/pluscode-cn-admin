import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Call once on a worker thread, before opening PlusCodeIndex. */
public final class AndroidDatabase {
    private AndroidDatabase() {}

    public static synchronized File prepare(Context context, String expectedSha256) throws IOException {
        File target = new File(context.getFilesDir(), "pluscode-admin-v2-20260907.sqlite");
        if (target.isFile() && matches(target, expectedSha256)) return target;
        File temporary = File.createTempFile("pluscode-", ".sqlite.tmp", context.getFilesDir());
        try {
            try (InputStream input = context.getAssets().open("pluscode_admin_v2.sqlite");
                 FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                output.getFD().sync();
            }
            if (!matches(temporary, expectedSha256)) throw new IOException("Database SHA-256 mismatch");
            if (!temporary.renameTo(target)) throw new IOException("Cannot install database");
            return target;
        } finally {
            if (temporary.exists() && !temporary.delete()) temporary.deleteOnExit();
        }
    }

    private static boolean matches(File file, String expected) throws IOException {
        if (expected == null || !expected.matches("[0-9a-fA-F]{64}"))
            throw new IllegalArgumentException("A SHA-256 from release_manifest.json is required");
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = new FileInputStream(file)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
            }
            StringBuilder hex = new StringBuilder(64);
            for (byte value : digest.digest()) hex.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
            return hex.toString().equalsIgnoreCase(expected);
        } catch (NoSuchAlgorithmException error) { throw new IOException(error); }
    }
}
