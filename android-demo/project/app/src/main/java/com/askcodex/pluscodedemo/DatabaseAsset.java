package com.askcodex.pluscodedemo;

import android.content.Context;
import java.io.*;
import java.security.MessageDigest;
import java.util.Locale;

final class DatabaseAsset {
    static final String SHA = "9abaed308d8d1286fd08fe6ab1517d5345198892a5873dfeb2dbd34b0687fe4f";
    interface Progress { void update(String message); }
    static File prepare(Context context, Progress progress) throws Exception {
        File target = new File(context.getFilesDir(), "pluscode-v2-" + SHA.substring(0, 12) + ".sqlite");
        progress.update("校验全国数据库…");
        if (target.isFile() && hash(target).equals(SHA)) return target;
        File pending = File.createTempFile("database-", ".tmp", context.getFilesDir());
        try {
            try (InputStream in = context.getAssets().open("pluscode_admin_v2.sqlite");
                 FileOutputStream out = new FileOutputStream(pending)) {
                byte[] buffer = new byte[65536];
                long total = 0, last = 0;
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                    total += read;
                    if (total - last > 8 * 1024 * 1024) {
                        progress.update("准备全国数据库 " + total * 100 / 220561408 + "%");
                        last = total;
                    }
                }
                out.getFD().sync();
            }
            progress.update("校验全国数据库…");
            if (!SHA.equals(hash(pending))) throw new IOException("数据库校验失败");
            if (!pending.renameTo(target)) throw new IOException("无法写入私有数据库");
            return target;
        } finally { if (pending.exists()) pending.delete(); }
    }
    private static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = in.read(buffer)) != -1) digest.update(buffer, 0, read);
        }
        StringBuilder hex = new StringBuilder();
        for (byte b : digest.digest()) hex.append(String.format(Locale.ROOT, "%02x", b & 255));
        return hex.toString();
    }
}
