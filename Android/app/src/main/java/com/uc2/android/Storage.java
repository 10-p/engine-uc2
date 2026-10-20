package com.uc2.android;

import android.os.StatFs;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * File helpers of the launcher: free space and folder sizes, the incremental copy of a game folder
 * (shared storage to internal), copying single files, and recursive delete. The long ones (dirSize,
 * syncTree, deleteTree) run on a background thread; the {@link Listener} is called on that thread.
 */
final class Storage {
    private Storage() {}

    interface Listener {
        /** Progress: {@code doneBytes} of {@code totalBytes} looked at (skipped files included, so the bar
         *  moves evenly), {@code copiedBytes} of them written. */
        void onProgress(long doneBytes, long totalBytes, long copiedBytes, String currentRel);
    }

    /** Bytes free for the app on the volume that holds {@code path}, -1 if unknown. */
    static long freeBytes(File path) {
        try {
            return new StatFs(path.getAbsolutePath()).getAvailableBytes();
        } catch (Throwable t) {
            return -1L;
        }
    }

    /** The bytes of every regular file under {@code dir} (0 if it does not exist). */
    static long dirSize(File dir) {
        if (dir == null || !dir.exists()) return 0L;
        if (dir.isFile()) return dir.length();
        long sum = 0L;
        File[] kids = dir.listFiles();
        if (kids != null) for (File k : kids) sum += dirSize(k);
        return sum;
    }

    /**
     * Copies the tree at {@code src} into {@code dst}, a file only when the destination is missing or has
     * another size: running it again adds what is new or changed, and resumes a copy that stopped (out of
     * space, the app closed). Each file is written to a {@code .part} file first and renamed when complete,
     * so a cut copy never leaves a file of the right name and the wrong content.
     *
     * Returns null when done, or what stopped it (out of space, an I/O error).
     */
    static String syncTree(File src, File dst, Listener cb) {
        if (src == null || !src.isDirectory()) return "Nothing to copy at " + src;
        long total = dirSize(src);
        long[] progress = new long[2]; // {doneBytes, copiedBytes}
        try {
            copyRecursive(src, dst, src, total, progress, cb);
            return null;
        } catch (IOException e) {
            String msg = e.getMessage() == null ? e.toString() : e.getMessage();
            if (msg.contains("ENOSPC") || msg.toLowerCase(Locale.US).contains("no space")) {
                return "Out of space after copying " + human(progress[1]) + ": internal storage is full. "
                        + "Free some space, or run the game from shared storage.";
            }
            return "Stopped after " + human(progress[1]) + " copied (" + msg + ")";
        }
    }

    private static void copyRecursive(File src, File dst, File root, long total, long[] progress, Listener cb)
            throws IOException {
        File[] kids = src.listFiles();
        if (kids == null) return;
        for (File k : kids) {
            File target = new File(dst, k.getName());
            if (k.isDirectory()) {
                copyRecursive(k, target, root, total, progress, cb);
            } else {
                if (!target.exists() || target.length() != k.length()) {
                    File parent = target.getParentFile();
                    if (parent != null && !parent.exists() && !parent.mkdirs())
                        throw new IOException("cannot create " + parent);
                    copyFile(k, target);
                    progress[1] += k.length();
                }
                progress[0] += k.length();
                if (cb != null) cb.onProgress(progress[0], total, progress[1], rel(root, k));
            }
        }
    }

    private static void copyFile(File src, File dst) throws IOException {
        InputStream in = new FileInputStream(src);
        try {
            copyStream(in, dst);
        } finally {
            try { in.close(); } catch (IOException ignored) {}
        }
    }

    /** Writes {@code in} to {@code dst} through a {@code .part} file, replacing {@code dst}. */
    static void copyStream(InputStream in, File dst) throws IOException {
        File tmp = new File(dst.getAbsolutePath() + ".part");
        OutputStream out = new FileOutputStream(tmp);
        try {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.flush();
        } finally {
            try { out.close(); } catch (IOException ignored) {}
        }
        if (dst.exists() && !dst.delete()) { tmp.delete(); throw new IOException("cannot replace " + dst); }
        if (!tmp.renameTo(dst)) { tmp.delete(); throw new IOException("cannot finish " + dst); }
    }

    /**
     * Copies one file over the destination (exporting settings and saves). Null when copied and when
     * {@code src} does not exist (nothing to export); the error otherwise.
     */
    static String copyFileInto(File src, File dst) {
        if (src == null || !src.isFile()) return null;
        try {
            File parent = dst.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs())
                throw new IOException("cannot create " + parent);
            copyFile(src, dst);
            return null;
        } catch (IOException e) {
            return e.getMessage() == null ? e.toString() : e.getMessage();
        }
    }

    /** Deletes {@code f} and everything under it; true when all of it went. */
    static boolean deleteTree(File f) {
        if (f == null || !f.exists()) return true;
        boolean ok = true;
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) for (File k : kids) ok &= deleteTree(k);
        }
        return f.delete() && ok;
    }

    private static String rel(File root, File f) {
        String rp = root.getAbsolutePath(), fp = f.getAbsolutePath();
        return fp.startsWith(rp) && fp.length() > rp.length() ? fp.substring(rp.length() + 1) : f.getName();
    }

    static String human(long bytes) {
        if (bytes < 0) return "?";
        if (bytes < 1024) return bytes + " B";
        double kb = bytes / 1024.0;
        if (kb < 1024) return String.format(Locale.US, "%.0f KB", kb);
        double mb = kb / 1024.0;
        if (mb < 1024) return String.format(Locale.US, "%.1f MB", mb);
        return String.format(Locale.US, "%.2f GB", mb / 1024.0);
    }
}
