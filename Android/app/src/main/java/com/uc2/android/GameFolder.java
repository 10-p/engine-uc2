package com.uc2.android;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Environment;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;

/**
 * Where the game folder is: a folder named UC2 (System, Maps, Textures, Sounds, Music, Movies, ...) on
 * shared storage, the complete game, slow to read through Android's storage layer; or the app's internal
 * copy of it, fast, deleted with the app. The launcher chooses and passes the choice to the game process
 * on the Intent ({@link GameActivity#EXTRA_STORAGE_MODE}).
 */
final class GameFolder {
    private static final String TAG = "UC2";
    static final String NAME = "UC2";

    private GameFolder() {}

    /** The internal copy: {@code getFilesDir()/UC2}. */
    static File internalRoot(Context ctx) { return new File(ctx.getFilesDir(), NAME); }

    /** A folder is a game folder when its System holds the engine's first package. */
    static boolean isGameFolder(File root) {
        return root != null && new File(new File(root, "System"), "Core.u").isFile();
    }

    /**
     * The game folder on shared storage: UC2 at the top of the primary shared storage (/sdcard/UC2), else
     * at the top of another volume (a physical SD card), else in this app's own folder on a volume
     * (Android/data/com.uc2.android/files/UC2). The primary location when none is found.
     */
    static File externalRoot(Context ctx) {
        File primary = new File(Environment.getExternalStorageDirectory(), NAME);
        if (isGameFolder(primary)) return primary;
        try {
            for (File appDir : ctx.getExternalFilesDirs(null)) {
                if (appDir == null) continue;
                // appDir is <volume>/Android/data/<package>/files: the volume is four levels up.
                File vol = appDir;
                for (int i = 0; i < 4 && vol != null; i++) vol = vol.getParentFile();
                if (vol != null && isGameFolder(new File(vol, NAME))) return new File(vol, NAME);
                if (isGameFolder(new File(appDir, NAME))) return new File(appDir, NAME);
            }
        } catch (Throwable t) {
            Log.w(TAG, "could not look through the storage volumes", t);
        }
        return primary;
    }

    /**
     * Installs the APK's System files (assets/system: the Android inis) into the folder's System,
     * each only where it differs, so the folder's Avaris.ini and User.ini are derived from them when they
     * do not exist yet. Returns how many files were written.
     */
    static int installSystemFiles(Context ctx, File root) {
        File system = new File(root, "System");
        AssetManager assets = ctx.getAssets();
        int written = 0;
        try {
            String[] names = assets.list("system");
            if (names == null) return 0;
            for (String name : names) {
                byte[] data = readAll(assets.open("system/" + name));
                File dst = new File(system, name);
                if (dst.isFile() && dst.length() == data.length && Arrays.equals(readAll(new FileInputStream(dst)), data))
                    continue;
                Storage.copyStream(new java.io.ByteArrayInputStream(data), dst);
                written++;
                Log.i(TAG, "installed " + dst + " from the APK");
            }
        } catch (IOException e) {
            Log.e(TAG, "could not install the APK's System files into " + system, e);
        }
        return written;
    }

    private static byte[] readAll(InputStream in) throws IOException {
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[1 << 14];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } finally {
            in.close();
        }
    }
}
