package com.uc2.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

/**
 * The launcher. Asks for storage access (the game folder is read in place from shared storage), then
 * shows the storage page: which game folder the game runs from (shared storage, or the app's internal
 * copy, which is much faster to read), copying the shared folder into internal storage (only what is
 * new or changed), clearing the internal copy, and copying the internal copy's settings and saves back
 * to shared storage. Start game launches {@link GameActivity} in its own process with the choice on the
 * Intent; it also starts by itself after a countdown that the first touch stops.
 *
 * The page is built in code (the app has no layouts and no support libraries).
 */
public class MainActivity extends Activity {
    private static final String TAG = "UC2";
    private static final int COUNTDOWN_SECS = 10;
    private static final String PREFS = "launcher";
    private static final String PREF_EXTERNAL = "runFromExternal";

    private boolean launcherShown = false;
    private boolean opRunning = false;
    private boolean gameLaunched = false;

    private Switch storageSwitch;
    private Button startButton, copyButton, clearButton, exportButton;
    private TextView statusText, progressText;
    private ProgressBar progressBar;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean countdownActive = false;
    private int countdownRemaining = 0;
    private final Runnable countdownTick = new Runnable() {
        @Override public void run() {
            if (!countdownActive) return;
            countdownRemaining--;
            if (countdownRemaining <= 0) {
                countdownActive = false;
                launchGame();
            } else {
                startButton.setText("Start game (" + countdownRemaining + ")");
                ui.postDelayed(this, 1000L);
            }
        }
    };

    @Override
    protected void onResume() {
        super.onResume();
        if (!hasStorageAccess()) {
            requestStorageAccess();
            return;
        }
        gameLaunched = false; // back from the game, or the first show: Start works again
        if (!launcherShown) {
            showLauncher();
        } else {
            cancelCountdown(true);
            refreshStatusAsync();
        }
    }

    // ---- storage access -------------------------------------------------------------------------

    private boolean hasStorageAccess() {
        return Environment.isExternalStorageManager();
    }

    /** The system's All files access page for this app; onResume checks again on the way back. */
    private void requestStorageAccess() {
        Toast.makeText(this, "Allow UC2 \"All files access\", then come back.", Toast.LENGTH_LONG).show();
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            Log.e(TAG, "no per-app All files access page; opening the list", e);
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    // ---- the page -------------------------------------------------------------------------------

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private void showLauncher() {
        launcherShown = true;

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xFF0B0E14);
        scroll.setFillViewport(true);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(24), dp(24), dp(24), dp(24));
        col.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        scroll.addView(col);

        TextView title = new TextView(this);
        title.setText("UNREAL CHAMPIONSHIP 2");
        title.setTextColor(0xFFE0A030);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, dp(18));
        col.addView(title);

        startButton = new Button(this);
        startButton.setText("Start game");
        startButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f);
        startButton.setAllCaps(false);
        col.addView(startButton, wide(dp(68)));
        startButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { launchGame(); }
        });

        TextView pick = new TextView(this);
        pick.setText("Run the game from:");
        pick.setTextColor(0xFFB8BCC2);
        pick.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
        pick.setPadding(0, dp(20), 0, dp(4));
        col.addView(pick);

        // Off = internal storage, on = shared storage.
        storageSwitch = new Switch(this);
        storageSwitch.setTextColor(0xFFECEFF3);
        storageSwitch.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        storageSwitch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean checked) {
                applyStorageSwitchText();
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(PREF_EXTERNAL, checked).apply();
            }
        });
        col.addView(storageSwitch);

        statusText = new TextView(this);
        statusText.setTextColor(0xFF9AA0A8);
        statusText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f);
        statusText.setPadding(0, dp(14), 0, dp(14));
        statusText.setText("Looking at the storage…");
        col.addView(statusText);

        copyButton = mkButton(col, "Copy the game into internal storage");
        copyButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doCopyToInternal(); }
        });
        clearButton = mkButton(col, "Clear internal storage");
        clearButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doClearInternal(); }
        });
        exportButton = mkButton(col, "Copy settings and saves to shared storage");
        exportButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doExportSettings(); }
        });

        progressText = new TextView(this);
        progressText.setTextColor(0xFF9AA0A8);
        progressText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f);
        progressText.setPadding(0, dp(16), 0, dp(4));
        progressText.setVisibility(View.GONE);
        col.addView(progressText);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setVisibility(View.GONE);
        col.addView(progressBar, wide(dp(12)));

        TextView hint = new TextView(this);
        hint.setTextColor(0xFF6E747C);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f);
        hint.setPadding(0, dp(24), 0, 0);
        hint.setText("The game folder is a folder named UC2 at the top of the shared storage:\n"
                + new File(Environment.getExternalStorageDirectory(), GameFolder.NAME)
                + "\nwith the game's System, Maps, Textures, Sounds, Music, Movies… folders in it. A UC2 folder at "
                + "the top of an SD card is found too. The internal copy is made from it, and is deleted with the app.");
        col.addView(hint);

        setContentView(scroll);

        // The last choice; the first time, internal when there is a copy there.
        boolean external = getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_EXTERNAL, !GameFolder.isGameFolder(GameFolder.internalRoot(this)));
        storageSwitch.setChecked(external);
        applyStorageSwitchText();

        refreshStatusAsync();
        startCountdown();
    }

    private Button mkButton(LinearLayout col, String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f);
        col.addView(b, wide(dp(50)));
        return b;
    }

    private LinearLayout.LayoutParams wide(int height) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, height);
        lp.topMargin = dp(6);
        return lp;
    }

    private void applyStorageSwitchText() {
        storageSwitch.setText(storageSwitch.isChecked() ? "Shared storage (slower)" : "Internal storage (fast)");
    }

    // ---- start and the countdown ----------------------------------------------------------------

    private void startCountdown() {
        countdownActive = true;
        countdownRemaining = COUNTDOWN_SECS;
        startButton.setText("Start game (" + countdownRemaining + ")");
        ui.postDelayed(countdownTick, 1000L);
    }

    private void cancelCountdown(boolean relabel) {
        if (!countdownActive) return;
        countdownActive = false;
        ui.removeCallbacks(countdownTick);
        if (relabel && startButton != null) startButton.setText("Start game");
    }

    /** The first touch stops the countdown and still does what it touched. */
    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (countdownActive && ev.getActionMasked() == MotionEvent.ACTION_DOWN) cancelCountdown(true);
        return super.dispatchTouchEvent(ev);
    }

    private void launchGame() {
        if (opRunning || gameLaunched) return; // not during a copy, and once (countdown and tap together)
        String mode = storageSwitch.isChecked() ? GameActivity.MODE_EXTERNAL : GameActivity.MODE_INTERNAL;
        File root = GameActivity.MODE_INTERNAL.equals(mode) ? GameFolder.internalRoot(this) : GameFolder.externalRoot(this);
        if (!GameFolder.isGameFolder(root)) {
            cancelCountdown(true);
            showMessage(GameActivity.MODE_INTERNAL.equals(mode)
                    ? "Internal storage has no game: copy it there first, or switch to shared storage."
                    : "No game folder at " + root + " (it needs System/Core.u): copy the game there first.");
            return;
        }
        gameLaunched = true;
        cancelCountdown(false);
        Log.i(TAG, "starting the game from " + root + " (" + mode + ")");
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra(GameActivity.EXTRA_STORAGE_MODE, mode);
        // An engine command line this launcher was started with (adb: GameActivity is not exported).
        String extraArgs = getIntent() != null ? getIntent().getStringExtra(GameActivity.EXTRA_ARGS) : null;
        if (extraArgs != null && !extraArgs.trim().isEmpty()) {
            i.putExtra(GameActivity.EXTRA_ARGS, extraArgs);
            Log.i(TAG, "engine arguments: " + extraArgs);
        }
        startActivity(i);
        // This activity stays: Back from the game comes here.
    }

    // ---- storage operations ---------------------------------------------------------------------

    private void doCopyToInternal() {
        if (opRunning) return;
        cancelCountdown(true);
        final File src = GameFolder.externalRoot(this);
        final File dst = GameFolder.internalRoot(this);
        if (!GameFolder.isGameFolder(src)) {
            showMessage("No game folder at " + src + " (it needs System/Core.u).");
            return;
        }
        startOp("Copying into internal storage…");
        new Thread(new Runnable() {
            @Override public void run() {
                final String err = Storage.syncTree(src, dst, throttledListener());
                final String msg = err == null ? "Copied: " + dst : err;
                ui.post(new Runnable() { @Override public void run() { finishOp(msg); } });
            }
        }, "uc2-copy").start();
    }

    private void doClearInternal() {
        if (opRunning) return;
        cancelCountdown(true);
        final File dst = GameFolder.internalRoot(this);
        if (!dst.exists()) { showMessage("Internal storage has no game."); return; }
        new AlertDialog.Builder(this)
                .setTitle("Clear internal storage?")
                .setMessage("Delete the internal copy at\n" + dst + "\n(" + Storage.human(Storage.dirSize(dst))
                        + "), its settings and saves included?")
                .setPositiveButton("Delete", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        startOp("Clearing internal storage…");
                        new Thread(new Runnable() {
                            @Override public void run() {
                                final boolean ok = Storage.deleteTree(dst);
                                final String msg = ok ? "Internal storage cleared." : "Some files could not be deleted.";
                                ui.post(new Runnable() { @Override public void run() { finishOp(msg); } });
                            }
                        }, "uc2-clear").start();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void doExportSettings() {
        if (opRunning) return;
        cancelCountdown(true);
        final File internal = GameFolder.internalRoot(this);
        final File external = GameFolder.externalRoot(this);
        if (!GameFolder.isGameFolder(internal)) {
            showMessage("Internal storage has no game to copy settings from.");
            return;
        }
        startOp("Copying settings and saves to shared storage…");
        new Thread(new Runnable() {
            @Override public void run() {
                // The derived inis, the saved games and the player profiles.
                StringBuilder err = new StringBuilder();
                int files = 0;
                for (String rel : new String[] { "System/Avaris.ini", "System/User.ini" }) {
                    File s = new File(internal, rel);
                    if (!s.isFile()) continue;
                    String e = Storage.copyFileInto(s, new File(external, rel));
                    if (e != null) err.append(rel).append(": ").append(e).append('\n');
                    else files++;
                }
                StringBuilder dirs = new StringBuilder();
                for (String rel : new String[] { "Save", "Saves" }) {
                    File s = new File(internal, rel);
                    if (!s.isDirectory()) continue;
                    String e = Storage.syncTree(s, new File(external, rel), throttledListener());
                    if (e != null) err.append(rel).append("/: ").append(e).append('\n');
                    else dirs.append(' ').append(rel).append('/');
                }
                final String msg = err.length() > 0 ? "Copied with errors:\n" + err
                        : "Copied " + files + " settings file(s)" + (dirs.length() > 0 ? " and" + dirs : "") + " to " + external;
                ui.post(new Runnable() { @Override public void run() { finishOp(msg); } });
            }
        }, "uc2-export").start();
    }

    private Storage.Listener throttledListener() {
        return new Storage.Listener() {
            long lastMs = 0L;
            @Override public void onProgress(long done, long total, long copied, String rel) {
                long now = SystemClock.uptimeMillis();
                if (now - lastMs < 150L && done < total) return; // at most ~7 updates a second, always the last
                lastMs = now;
                final int pct = total > 0 ? (int) (done * 100L / total) : 0;
                final String line = "Copied " + Storage.human(copied) + "  •  " + rel;
                ui.post(new Runnable() { @Override public void run() { updateProgress(line, pct); } });
            }
        };
    }

    // ---- operation state ------------------------------------------------------------------------

    private void startOp(String label) {
        opRunning = true;
        setControlsEnabled(false);
        progressText.setVisibility(View.VISIBLE);
        progressText.setText(label);
        progressBar.setVisibility(View.VISIBLE);
        progressBar.setProgress(0);
    }

    private void updateProgress(String line, int pct) {
        progressText.setText(line);
        progressBar.setProgress(pct);
    }

    private void finishOp(String message) {
        opRunning = false;
        setControlsEnabled(true);
        progressBar.setVisibility(View.GONE);
        progressText.setVisibility(View.VISIBLE);
        progressText.setText(message);
        Log.i(TAG, message);
        refreshStatusAsync();
    }

    private void setControlsEnabled(boolean enabled) {
        startButton.setEnabled(enabled);
        copyButton.setEnabled(enabled);
        clearButton.setEnabled(enabled);
        exportButton.setEnabled(enabled);
        storageSwitch.setEnabled(enabled);
    }

    private void refreshStatusAsync() {
        if (statusText == null) return;
        new Thread(new Runnable() {
            @Override public void run() {
                File internal = GameFolder.internalRoot(MainActivity.this);
                File external = GameFolder.externalRoot(MainActivity.this);
                long free = Storage.freeBytes(getFilesDir());
                boolean intReady = GameFolder.isGameFolder(internal);
                boolean extReady = GameFolder.isGameFolder(external);
                final String s = "Internal storage free: " + Storage.human(free) + '\n'
                        + "Internal copy: " + (intReady ? Storage.human(Storage.dirSize(internal)) + "  " + internal : "none") + '\n'
                        + "Shared storage: " + (extReady ? Storage.human(Storage.dirSize(external)) + "  " + external : "no game folder at " + external);
                ui.post(new Runnable() { @Override public void run() { statusText.setText(s); } });
            }
        }, "uc2-status").start();
    }

    /** A message in the progress line and as a toast. */
    private void showMessage(String msg) {
        if (progressText != null) {
            progressBar.setVisibility(View.GONE);
            progressText.setVisibility(View.VISIBLE);
            progressText.setText(msg);
        }
        Log.i(TAG, msg);
        Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
    }
}
