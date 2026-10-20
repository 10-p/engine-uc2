package com.uc2.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.Locale;

/**
 * The launcher: which game folder the game runs from, getting the game into it, and the resolution.
 *
 * The engine reads its game folder by path. Internal storage (the app's own, fast, deleted with the app)
 * needs no permission: "Choose the UC2 folder" opens Android's folder picker, which gives the app that
 * folder only, and "Copy to internal storage" copies it in (only what is new or changed; with no folder
 * chosen yet it opens the picker first). A UC2 folder in the app's own folder on a volume
 * (Android/data/com.uc2.android/files/UC2) is read in place without permission too; running in place from
 * anywhere else on shared storage (/sdcard/UC2, an SD card's UC2) needs All files access: starting from there
 * without it asks to allow it or to copy the game to internal storage instead. "Clear internal storage" deletes the
 * internal copy; "Copy settings and saves" writes the internal copy's inis and saves back to the chosen
 * folder. The resolution scale is the frame's size against the screen's (above 1: supersampling).
 *
 * Start game launches {@link GameActivity} in its own process with these choices on the Intent; it also
 * starts by itself after a countdown that the first touch or key stops. The page is built in code (the app
 * has no layouts and no support libraries).
 */
public class MainActivity extends Activity {
    private static final String TAG = "UC2";
    private static final int COUNTDOWN_SECS = 10;
    private static final int REQ_PICK_FOLDER = 2;
    private static final String PREFS = "launcher";
    private static final String PREF_EXTERNAL = "runFromExternal";
    private static final String PREF_FOLDER = "folderUri";
    private static final String PREF_SCALE = "renderScale";
    // The scale's slider: 0.25 to 1.00 in steps of 0.05; the field also takes values from 0.1 to 4.
    private static final float SCALE_MIN = 0.25f, SCALE_STEP = 0.05f, FIELD_MIN = 0.1f, FIELD_MAX = 4f;
    private static final int SCALE_STEPS = 15;
    // The Android ini's [SDL2Drv.SDL2Client] RenderScale.
    private static final float SCALE_DEFAULT = 0.5f;

    private boolean launcherShown = false;
    private boolean opRunning = false;
    private boolean gameLaunched = false;
    private boolean scaleUpdating = false;
    private boolean startWhenAllowed = false;  // back from the access page with access: start the game
    private boolean copyAfterPick = false;     // a folder picked for a copy: copy it

    private Switch storageSwitch;
    private Button startButton, pickButton, copyButton, clearButton, exportButton;
    private TextView statusText, progressText, scaleText;
    private SeekBar scaleBar;
    private EditText scaleField;
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

    private SharedPreferences prefs() { return getSharedPreferences(PREFS, MODE_PRIVATE); }

    @Override
    protected void onResume() {
        super.onResume();
        gameLaunched = false; // back from the game, or the first show: Start works again
        if (!launcherShown) {
            showLauncher();
        } else {
            cancelCountdown(true);
            refreshStatusAsync();
            if (startWhenAllowed) {
                startWhenAllowed = false;
                if (Environment.isExternalStorageManager()) launchGame();
            }
        }
    }

    // ---- the page -------------------------------------------------------------------------------

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView label(LinearLayout col, String text, float sp, int color, int top) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setPadding(0, dp(top), 0, dp(4));
        col.addView(t);
        return t;
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

        label(col, "Run the game from:", 14f, 0xFFB8BCC2, 20);
        // Off = internal storage, on = shared storage.
        storageSwitch = new Switch(this);
        storageSwitch.setTextColor(0xFFECEFF3);
        storageSwitch.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        storageSwitch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean checked) {
                applyStorageSwitchText();
                prefs().edit().putBoolean(PREF_EXTERNAL, checked).apply();
            }
        });
        col.addView(storageSwitch);

        statusText = label(col, "Looking at the storage…", 13f, 0xFF9AA0A8, 14);
        // A copy's progress and the last result, under the status where they are in view.
        progressText = label(col, "", 12f, 0xFF9AA0A8, 10);
        progressText.setVisibility(View.GONE);
        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setVisibility(View.GONE);
        col.addView(progressBar, wide(dp(12)));

        pickButton = mkButton(col, "Choose the UC2 folder…");
        pickButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { pickFolder(); }
        });
        copyButton = mkButton(col, "Copy to internal storage");
        copyButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doCopyToInternal(); }
        });
        clearButton = mkButton(col, "Clear internal storage");
        clearButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doClearInternal(); }
        });
        exportButton = mkButton(col, "Copy settings and saves to the UC2 folder");
        exportButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doExportSettings(); }
        });

        buildScaleRow(col);

        label(col, "Choose the UC2 folder (on the device or an SD card) and copy it to internal storage for the fastest "
                + "loading. Playing from the UC2 folder directly needs access to all files.", 11f, 0xFF6E747C, 24);

        setContentView(scroll);
        // In touch mode the first D-pad press only leaves touch mode and never reaches dispatchKeyEvent.
        scroll.getViewTreeObserver().addOnTouchModeChangeListener(new ViewTreeObserver.OnTouchModeChangeListener() {
            @Override public void onTouchModeChanged(boolean inTouchMode) { if (!inTouchMode) cancelCountdown(true); }
        });

        // The last choice; the first time, internal when there is a copy there.
        storageSwitch.setChecked(prefs().getBoolean(PREF_EXTERNAL, !GameFolder.isGameFolder(GameFolder.internalRoot(this))));
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
        storageSwitch.setText(storageSwitch.isChecked() ? "UC2 folder (slower)" : "Internal storage (fast)");
    }

    // ---- resolution scale -----------------------------------------------------------------------

    private void buildScaleRow(LinearLayout col) {
        label(col, "Resolution scale", 14f, 0xFFB8BCC2, 20);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        col.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        scaleBar = new SeekBar(this);
        scaleBar.setMax(SCALE_STEPS);
        row.addView(scaleBar, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        scaleField = new EditText(this);
        scaleField.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
        scaleField.setImeOptions(EditorInfo.IME_ACTION_DONE);
        scaleField.setSingleLine(true);
        scaleField.setTextColor(0xFFECEFF3);
        scaleField.setGravity(Gravity.CENTER);
        row.addView(scaleField, new LinearLayout.LayoutParams(dp(88), ViewGroup.LayoutParams.WRAP_CONTENT));

        scaleText = label(col, "", 12f, 0xFF9AA0A8, 2);

        float scale = prefs().getFloat(PREF_SCALE, SCALE_DEFAULT);
        showScale(scale, true, true);

        scaleBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (!fromUser || scaleUpdating) return;
                float v = SCALE_MIN + progress * SCALE_STEP;
                setScale(v, false, true);
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { cancelCountdown(true); }
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });
        scaleField.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                if (scaleUpdating) return;
                float v;
                try { v = Float.parseFloat(s.toString().trim()); } catch (NumberFormatException e) { v = -1f; }
                if (v >= FIELD_MIN && v <= FIELD_MAX) setScale(v, true, false);
                else scaleText.setText(String.format(Locale.US, "Enter a scale from %.1f to %.0f.", FIELD_MIN, FIELD_MAX));
            }
        });
    }

    private void setScale(float v, boolean moveBar, boolean writeField) {
        prefs().edit().putFloat(PREF_SCALE, v).apply();
        showScale(v, moveBar, writeField);
    }

    /** Shows {@code v} on the slider (at most its end) and in the field, and the frame it gives. */
    private void showScale(float v, boolean moveBar, boolean writeField) {
        scaleUpdating = true;
        if (moveBar) {
            int step = Math.round((Math.min(v, 1f) - SCALE_MIN) / SCALE_STEP);
            scaleBar.setProgress(Math.max(0, Math.min(SCALE_STEPS, step)));
        }
        if (writeField) scaleField.setText(formatScale(v));
        scaleUpdating = false;

        // The screen in landscape, the way the game holds it.
        Rect b = getWindowManager().getMaximumWindowMetrics().getBounds();
        int sw = Math.max(b.width(), b.height()), sh = Math.min(b.width(), b.height());
        int fw = Math.round(sw * v), fh = Math.round(sh * v);
        scaleText.setText(String.format(Locale.US, "The game draws %d × %d for the %d × %d screen%s", fw, fh, sw, sh,
                v > 1f ? " (supersampling)" : ""));
    }

    private static String formatScale(float v) {
        String s = String.format(Locale.US, "%.2f", v);
        return s.endsWith("0") ? s.substring(0, s.length() - 1) : s;
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

    /** So does the first key or pad button. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        if (countdownActive && ev.getAction() == KeyEvent.ACTION_DOWN) cancelCountdown(true);
        return super.dispatchKeyEvent(ev);
    }

    private void launchGame() {
        if (opRunning || gameLaunched) return; // not during a copy, and once (countdown and tap together)
        String mode = storageSwitch.isChecked() ? GameActivity.MODE_EXTERNAL : GameActivity.MODE_INTERNAL;
        if (GameActivity.MODE_INTERNAL.equals(mode)) {
            if (!GameFolder.isGameFolder(GameFolder.internalRoot(this))) {
                cancelCountdown(true);
                showMessage("There's no game in internal storage yet. Copy it from the UC2 folder first.");
                return;
            }
        } else {
            File root = GameFolder.externalRoot(this);
            if (!GameFolder.isGameFolder(root)) {
                cancelCountdown(true);
                if (!Environment.isExternalStorageManager()) askAllFilesAccess();
                else showMessage("No game folder at " + root + " (it needs System/Core.u).");
                return;
            }
        }
        gameLaunched = true;
        cancelCountdown(false);
        float scale = prefs().getFloat(PREF_SCALE, SCALE_DEFAULT);
        Log.i(TAG, "starting the game (" + mode + ", resolution scale " + scale + ")");
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra(GameActivity.EXTRA_STORAGE_MODE, mode);
        i.putExtra(GameActivity.EXTRA_RENDER_SCALE, scale);
        // An engine command line this launcher was started with (adb: GameActivity is not exported).
        String extraArgs = getIntent() != null ? getIntent().getStringExtra(GameActivity.EXTRA_ARGS) : null;
        if (extraArgs != null && !extraArgs.trim().isEmpty()) i.putExtra(GameActivity.EXTRA_ARGS, extraArgs);
        startActivity(i);
        // This activity stays: Back from the game comes here.
    }

    /**
     * Playing in place from the UC2 folder needs All files access; copying the game to internal storage does not.
     * Allow access opens the system's page for it, and the game starts when the player comes back with it on.
     */
    private void askAllFilesAccess() {
        new AlertDialog.Builder(this)
                .setTitle("Allow access to all files?")
                .setMessage("To play from the UC2 folder, UC2 needs access to all files. Or copy the game to internal "
                        + "storage \u2014 no access needed.")
                .setNeutralButton("Copy to internal storage", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) { doCopyToInternal(); }
                })
                .setPositiveButton("Allow access", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        startWhenAllowed = true;
                        try {
                            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                    Uri.parse("package:" + getPackageName())));
                        } catch (Exception e) {
                            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                        }
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    // ---- the folder picker ----------------------------------------------------------------------

    private Uri chosenFolder() {
        String s = prefs().getString(PREF_FOLDER, null);
        return s == null ? null : Uri.parse(s);
    }

    private void pickFolder() {
        cancelCountdown(true);
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        // Opens at /sdcard/UC2 when it exists.
        i.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:" + GameFolder.NAME));
        try {
            startActivityForResult(i, REQ_PICK_FOLDER);
        } catch (Exception e) {
            showMessage("This device has no folder picker.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK_FOLDER) return;
        final boolean copy = copyAfterPick;
        copyAfterPick = false;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.w(TAG, "no persistable permission for " + tree, e);
        }
        prefs().edit().putString(PREF_FOLDER, tree.toString()).apply();
        Log.i(TAG, "chosen folder " + tree);
        new Thread(new Runnable() {
            @Override public void run() {
                final boolean ok = Storage.isGameTree(getContentResolver(), tree);
                ui.post(new Runnable() { @Override public void run() {
                    if (!ok) showMessage("That folder doesn't contain the game (no System/Core.u). Choose the UC2 folder.");
                    else if (copy) doCopyToInternal();
                    else showMessage("UC2 folder chosen: " + folderName(tree));
                    refreshStatusAsync();
                } });
            }
        }, "uc2-check").start();
    }

    /** "UC2 (shared storage)", "UC2 (SD card 3434-D61F)": a tree's document id is volume:path. */
    private static String folderName(Uri tree) {
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        String vol = colon > 0 ? id.substring(0, colon) : "";
        String path = colon >= 0 ? id.substring(colon + 1) : id;
        return (path.isEmpty() ? "/" : path) + ("primary".equals(vol) ? " (shared storage)" : vol.isEmpty() ? "" : " (SD card " + vol + ")");
    }

    // ---- storage operations ---------------------------------------------------------------------

    private void doCopyToInternal() {
        if (opRunning) return;
        cancelCountdown(true);
        final File dst = GameFolder.internalRoot(this);
        final Uri tree = chosenFolder();
        final File src = GameFolder.externalRoot(this);
        if (tree == null && !GameFolder.isGameFolder(src)) {
            // No folder yet: choose it, and the copy follows.
            copyAfterPick = true;
            pickFolder();
            return;
        }
        startOp("Copying to internal storage…");
        new Thread(new Runnable() {
            @Override public void run() {
                String err;
                if (tree != null) {
                    err = Storage.isGameTree(getContentResolver(), tree)
                            ? Storage.syncFromTree(getContentResolver(), tree, dst, throttledListener())
                            : "UC2 can't read the chosen folder anymore. Choose the UC2 folder again.";
                } else {
                    err = Storage.syncTree(src, dst, throttledListener());
                }
                final String error = err;
                ui.post(new Runnable() { @Override public void run() {
                    // A finished copy is what the game runs from.
                    if (error == null) storageSwitch.setChecked(false);
                    finishOp(error == null ? "Copy complete." : error);
                } });
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
        final Uri tree = chosenFolder();
        final File external = GameFolder.externalRoot(this);
        if (!GameFolder.isGameFolder(internal)) {
            showMessage("Internal storage has no game to copy settings from.");
            return;
        }
        if (tree == null && !GameFolder.isGameFolder(external)) {
            showMessage("Choose the UC2 folder first.");
            return;
        }
        startOp("Copying settings and saves…");
        new Thread(new Runnable() {
            @Override public void run() {
                // The derived inis, the saved games and the player profiles.
                String[] rels = { "System/Avaris.ini", "System/User.ini", "Save", "Saves" };
                StringBuilder err = new StringBuilder();
                int files = 0;
                if (tree != null) {
                    files = Storage.exportToTree(getContentResolver(), tree, internal, rels, err);
                } else {
                    for (String rel : rels) {
                        File s = new File(internal, rel);
                        if (s.isDirectory()) {
                            String e = Storage.syncTree(s, new File(external, rel), null);
                            if (e != null) err.append(rel).append(": ").append(e).append('\n');
                            else files++;
                        } else if (s.isFile()) {
                            String e = Storage.copyFileInto(s, new File(external, rel));
                            if (e != null) err.append(rel).append(": ").append(e).append('\n');
                            else files++;
                        }
                    }
                }
                final String msg = err.length() > 0 ? "Copied with errors:\n" + err
                        : "Copied the settings and saves (" + files + ") to " + (tree != null ? folderName(tree) : external.toString());
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
        pickButton.setEnabled(enabled);
        copyButton.setEnabled(enabled);
        clearButton.setEnabled(enabled);
        exportButton.setEnabled(enabled);
        storageSwitch.setEnabled(enabled);
    }

    private void refreshStatusAsync() {
        if (statusText == null) return;
        final Uri tree = chosenFolder();
        new Thread(new Runnable() {
            @Override public void run() {
                File internal = GameFolder.internalRoot(MainActivity.this);
                File external = GameFolder.externalRoot(MainActivity.this);
                long free = Storage.freeBytes(getFilesDir());
                boolean intReady = GameFolder.isGameFolder(internal);
                boolean extReady = GameFolder.isGameFolder(external);
                String chosen = tree == null ? "none" : folderName(tree)
                        + (Storage.isGameTree(getContentResolver(), tree) ? "" : "  (no System/Core.u, or no access)");
                String shared = extReady ? Storage.human(Storage.dirSize(external)) + "  " + external
                        : Environment.isExternalStorageManager() ? "no game folder at " + external
                        : "needs access to all files";
                final String s = "Internal storage free: " + Storage.human(free) + '\n'
                        + "Internal copy: " + (intReady ? Storage.human(Storage.dirSize(internal)) + "  " + internal : "none") + '\n'
                        + "Chosen UC2 folder: " + chosen + '\n'
                        + "UC2 folder, in place: " + shared;
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
