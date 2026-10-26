package com.uc2.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Point;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity, in the ":game" process (the engine starts once per process, so the process
 * ends with the game, {@link #onDestroy}). Before SDL starts SDL_main, the game folder chosen in the
 * launcher gets the APK's System files and becomes the engine's base directory (the JNI bridge sets the
 * environment and the working directory). Over SDL's surface lies the on-screen pad
 * ({@link PadOverlayView}), which drives a virtual SDL controller in the engine (SDL2Drv/Src/SDL2Android.cpp).
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "UC2";

    /** Which game folder (the launcher's choice): {@link #MODE_INTERNAL} or {@link #MODE_EXTERNAL}. */
    static final String EXTRA_STORAGE_MODE = "com.uc2.android.STORAGE_MODE";
    static final String MODE_INTERNAL = "internal";
    static final String MODE_EXTERNAL = "external";

    /**
     * An engine command line, whitespace-separated, given to the launcher (GameActivity is not exported)
     * and passed on, the desktop's command line: a map URL first, then switches, e.g.
     *
     *   adb shell am start -n com.uc2.android/.MainActivity --es com.uc2.android.ARGS 'DM-Roman -nosound'
     *
     * The launcher's countdown then starts the game without a touch.
     */
    static final String EXTRA_ARGS = "com.uc2.android.ARGS";

    /**
     * The frame's size as a multiple of the screen's (the launcher's resolution scale): below 1 fewer
     * pixels than the screen, above 1 more (supersampling). The engine gets it as
     * [SDL2Drv.SDL2Client] RenderScale, which it also keeps in the game folder's ini.
     */
    static final String EXTRA_RENDER_SCALE = "com.uc2.android.RENDER_SCALE";

    private AlertDialog exitDialog;

    private static boolean bridgeLoaded = false;

    static {
        // The bridge first, on its own: nativePrepareProcess runs before SDL loads its libraries.
        try {
            System.loadLibrary("uc2_android_bridge");
            bridgeLoaded = true;
        } catch (Throwable t) {
            Log.e(TAG, "could not load uc2_android_bridge", t);
        }
    }

    // android_bridge.cpp.
    private static native boolean nativePrepareProcess(String dataRoot, String homeDir);

    // The on-screen pad (SDL2Drv/Src/SDL2Android.cpp, in libUC2.so).
    static native void nativeTouchPadButton(int button, boolean down);
    static native void nativeTouchPadAxis(int axis, float value);
    static native int nativeTouchPadState();

    private PadOverlayView pad;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before super.onCreate, which starts the SDL thread and with it SDL_main.
        File root = resolveGameFolder();
        GameFolder.installSystemFiles(this, root);
        File home = new File(getFilesDir(), "home");
        if (bridgeLoaded) {
            boolean ok = nativePrepareProcess(root.getAbsolutePath(), home.getAbsolutePath());
            Log.i(TAG, "game folder " + root + (ok ? "" : ": the process set-up failed"));
        } else {
            Log.e(TAG, "no bridge: the engine will not find the game folder " + root);
        }
        super.onCreate(savedInstanceState);

        // A match can go on for long without a touch (a pad, spectating).
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // The whole screen is the game's: the window draws under the system bars, with no bar backgrounds of its
        // own. Without FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS the window of this legacy theme paints a black band over
        // the navigation bar's place (the home line) even while SDL's immersive mode hides the bar.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) getWindow().setNavigationBarContrastEnforced(false);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) getWindow().setDecorFitsSystemWindows(false);

        try {
            pad = new PadOverlayView(this);
            addContentView(pad, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        } catch (Throwable t) {
            Log.e(TAG, "could not add the on-screen pad", t);
        }
    }

    /**
     * The launcher's choice; with none (the game process started again from the recent apps), the
     * internal copy if there is one. An internal choice without an internal copy runs from shared storage.
     */
    private File resolveGameFolder() {
        String mode = getIntent() != null ? getIntent().getStringExtra(EXTRA_STORAGE_MODE) : null;
        File internal = GameFolder.internalRoot(this);
        if (MODE_EXTERNAL.equals(mode)) return GameFolder.externalRoot(this);
        if (GameFolder.isGameFolder(internal)) return internal;
        if (MODE_INTERNAL.equals(mode)) Log.w(TAG, "no internal copy at " + internal + ": running from shared storage");
        return GameFolder.externalRoot(this);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "UC2" };
    }

    @Override
    protected String[] getArguments() {
        // The engine writes its inis, its log and the saves into the game folder, as on a desktop. The
        // command line from adb comes first (a map URL must be the first argument), then the settings.
        ArrayList<String> args = new ArrayList<String>();
        String extra = getIntent() != null ? getIntent().getStringExtra(EXTRA_ARGS) : null;
        if (extra != null && !extra.trim().isEmpty()) args.addAll(Arrays.asList(extra.trim().split("\\s+")));
        float scale = getIntent() != null ? getIntent().getFloatExtra(EXTRA_RENDER_SCALE, 0f) : 0f;
        if (scale > 0f) args.add(String.format(Locale.US, "-ini:System:[SDL2Drv.SDL2Client]:RenderScale=%.2f", scale));
        Log.i(TAG, "engine arguments: " + args);
        return args.toArray(new String[0]);
    }

    // A real pad's input hides the on-screen pad (PadOverlayView.onRealPadInput); SDL still gets every event.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (pad != null && isPadPress(event)) pad.onRealPadInput();
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (pad != null && isPadPush(event)) pad.onRealPadInput();
        return super.dispatchGenericMotionEvent(event);
    }

    /** A game pad's button going down (a pad's own source, or a gamepad key code from any source). */
    static boolean isPadPress(KeyEvent e) {
        if (e.getAction() != KeyEvent.ACTION_DOWN || e.getRepeatCount() != 0) return false;
        return e.isFromSource(InputDevice.SOURCE_GAMEPAD) || e.isFromSource(InputDevice.SOURCE_JOYSTICK)
                || KeyEvent.isGamepadButton(e.getKeyCode());
    }

    private static final int[] PAD_AXES = {
        MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RX, MotionEvent.AXIS_RY,
        MotionEvent.AXIS_RZ, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_LTRIGGER,
        MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS, MotionEvent.AXIS_BRAKE,
    };

    /** A pad's stick, trigger or hat pushed at least half way (a resting stick's drift stays below). */
    static boolean isPadPush(MotionEvent e) {
        if (!e.isFromSource(InputDevice.SOURCE_JOYSTICK) || e.getActionMasked() != MotionEvent.ACTION_MOVE) return false;
        for (int axis : PAD_AXES) if (Math.abs(e.getAxisValue(axis)) >= 0.5f) return true;
        return false;
    }

    /**
     * Called by the engine (SDL2Drv's AskToExit) when Back has nothing left to go back to: the usual
     * Android confirmation. Exit quits through SDL, so the engine saves its settings and shuts down.
     */
    public void showExitDialog() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (exitDialog != null && exitDialog.isShowing()) return;
                exitDialog = new AlertDialog.Builder(GameActivity.this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                        .setTitle("Exit game?")
                        .setNegativeButton("Cancel", null)
                        .setPositiveButton("Exit", new android.content.DialogInterface.OnClickListener() {
                            @Override public void onClick(android.content.DialogInterface d, int which) {
                                Log.i(TAG, "exit confirmed");
                                SDLActivity.nativeSendQuit();
                            }
                        })
                        .create();
                exitDialog.show();
                // A pad's confirm button lands on Cancel, as console exit prompts do; Exit is one step away.
                android.widget.Button cancel = exitDialog.getButton(AlertDialog.BUTTON_NEGATIVE);
                if (cancel != null) cancel.requestFocus();
            }
        });
    }

    @Override
    protected void onDestroy() {
        // SDLActivity waits for SDL_main to return (the engine saved its settings and shut down).
        super.onDestroy();
        if (isFinishing()) {
            Log.i(TAG, "the game ended: ending its process");
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }

    /** The largest window's bounds: WindowMetrics from Android 11, the display's real size before. */
    static Rect maximumWindowBounds(Activity a) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) return a.getWindowManager().getMaximumWindowMetrics().getBounds();
        Point p = new Point();
        a.getWindowManager().getDefaultDisplay().getRealSize(p);
        return new Rect(0, 0, p.x, p.y);
    }
}
