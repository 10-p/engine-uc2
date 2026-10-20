package com.uc2.android;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
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

    // ---- a folder chosen with Android's folder picker: documents of a tree URI, no file paths --------------

    /** One document of a picked tree. */
    static final class Doc {
        final String id, name;
        final boolean dir;
        final long size;
        Doc(String id, String name, boolean dir, long size) { this.id = id; this.name = name; this.dir = dir; this.size = size; }
    }

    /** The documents in the tree's directory {@code parentId}. */
    static List<Doc> children(ContentResolver cr, Uri tree, String parentId) throws IOException {
        ArrayList<Doc> out = new ArrayList<Doc>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE };
        Cursor c = cr.query(uri, cols, null, null, null);
        if (c == null) throw new IOException("cannot list " + uri);
        try {
            while (c.moveToNext())
                out.add(new Doc(c.getString(0), c.getString(1), Document.MIME_TYPE_DIR.equals(c.getString(2)), c.isNull(3) ? 0L : c.getLong(3)));
        } finally {
            c.close();
        }
        return out;
    }

    /** The document named {@code name} in {@code parentId} (any case, as the engine looks names up), or null. */
    static Doc child(ContentResolver cr, Uri tree, String parentId, String name) throws IOException {
        for (Doc d : children(cr, tree, parentId)) if (d.name.equalsIgnoreCase(name)) return d;
        return null;
    }

    /** A picked folder is the game folder when its System holds the engine's first package. */
    static boolean isGameTree(ContentResolver cr, Uri tree) {
        try {
            Doc system = child(cr, tree, DocumentsContract.getTreeDocumentId(tree), "System");
            return system != null && system.dir && child(cr, tree, system.id, "Core.u") != null;
        } catch (Exception e) {
            return false;
        }
    }

    private static long treeSize(ContentResolver cr, Uri tree, String id) throws IOException {
        long sum = 0L;
        for (Doc d : children(cr, tree, id)) sum += d.dir ? treeSize(cr, tree, d.id) : d.size;
        return sum;
    }

    /**
     * {@link #syncTree} for a picked folder: copies the tree's files into {@code dst}, each only when the
     * destination is missing or has another size, through a {@code .part} file. Null when done, else what stopped it.
     */
    static String syncFromTree(ContentResolver cr, Uri tree, File dst, Listener cb) {
        long[] progress = new long[2]; // {doneBytes, copiedBytes}
        try {
            String root = DocumentsContract.getTreeDocumentId(tree);
            copyTreeRecursive(cr, tree, root, dst, "", treeSize(cr, tree, root), progress, cb);
            return null;
        } catch (IOException e) {
            String msg = e.getMessage() == null ? e.toString() : e.getMessage();
            if (msg.contains("ENOSPC") || msg.toLowerCase(Locale.US).contains("no space"))
                return "Out of space after copying " + human(progress[1]) + ": internal storage is full.";
            return "Stopped after " + human(progress[1]) + " copied (" + msg + ")";
        }
    }

    private static void copyTreeRecursive(ContentResolver cr, Uri tree, String id, File dst, String rel, long total,
            long[] progress, Listener cb) throws IOException {
        for (Doc d : children(cr, tree, id)) {
            File target = new File(dst, d.name);
            String r = rel.isEmpty() ? d.name : rel + "/" + d.name;
            if (d.dir) {
                copyTreeRecursive(cr, tree, d.id, target, r, total, progress, cb);
                continue;
            }
            if (!target.exists() || target.length() != d.size) {
                File parent = target.getParentFile();
                if (parent != null && !parent.exists() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
                InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, d.id));
                if (in == null) throw new IOException("cannot read " + r);
                try { copyStream(in, target); } finally { try { in.close(); } catch (IOException ignored) {} }
                progress[1] += d.size;
            }
            progress[0] += d.size;
            if (cb != null) cb.onProgress(progress[0], total, progress[1], r);
        }
    }

    /** The directory {@code name} in {@code parentId}, created if missing; its document id. */
    private static String treeDir(ContentResolver cr, Uri tree, String parentId, String name) throws IOException {
        Doc d = child(cr, tree, parentId, name);
        if (d != null) return d.id;
        Uri made = DocumentsContract.createDocument(cr, DocumentsContract.buildDocumentUriUsingTree(tree, parentId),
                Document.MIME_TYPE_DIR, name);
        if (made == null) throw new IOException("cannot create " + name);
        return DocumentsContract.getDocumentId(made);
    }

    /** Writes the file {@code src} as {@code name} in the tree's directory {@code parentId}, replacing it. */
    private static void writeToTree(ContentResolver cr, Uri tree, String parentId, String name, File src) throws IOException {
        Doc d = child(cr, tree, parentId, name);
        Uri uri = d != null ? DocumentsContract.buildDocumentUriUsingTree(tree, d.id)
                : DocumentsContract.createDocument(cr, DocumentsContract.buildDocumentUriUsingTree(tree, parentId),
                        "application/octet-stream", name);
        if (uri == null) throw new IOException("cannot create " + name);
        OutputStream out = cr.openOutputStream(uri, "wt");
        if (out == null) throw new IOException("cannot write " + name);
        InputStream in = new FileInputStream(src);
        try {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } finally {
            try { in.close(); } catch (IOException ignored) {}
            out.close();
        }
    }

    /**
     * Copies files and directories of the local folder {@code src} (paths relative to it, '/'-separated) into
     * the picked tree, replacing what is there. Returns how many files were written; errors go to {@code err}.
     */
    static int exportToTree(ContentResolver cr, Uri tree, File src, String[] rels, StringBuilder err) {
        int files = 0;
        String root = DocumentsContract.getTreeDocumentId(tree);
        for (String rel : rels) {
            File f = new File(src, rel);
            if (!f.exists()) continue;
            try {
                String parent = root;
                String[] parts = rel.split("/");
                for (int i = 0; i < parts.length - 1; i++) parent = treeDir(cr, tree, parent, parts[i]);
                files += exportFile(cr, tree, parent, f);
            } catch (IOException e) {
                err.append(rel).append(": ").append(e.getMessage() == null ? e.toString() : e.getMessage()).append('\n');
            }
        }
        return files;
    }

    private static int exportFile(ContentResolver cr, Uri tree, String parentId, File f) throws IOException {
        if (!f.isDirectory()) {
            writeToTree(cr, tree, parentId, f.getName(), f);
            return 1;
        }
        String dir = treeDir(cr, tree, parentId, f.getName());
        int files = 0;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) files += exportFile(cr, tree, dir, k);
        return files;
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
