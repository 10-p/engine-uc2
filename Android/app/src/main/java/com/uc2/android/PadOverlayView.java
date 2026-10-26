package com.uc2.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;

/**
 * The on-screen pad: an Xbox pad drawn over the game, whose buttons and sticks go to the engine's
 * virtual SDL controller (GameActivity's native calls; SDL2Drv/Src/SDL2Android.cpp), so the game reads it
 * as it reads any pad: the menus by their pad navigation, the match by the pad bindings. The mouse plays no
 * part (the client ignores it on Android).
 *
 * Two layouts, chosen by the engine's state: in the menus a large D-pad, A B X Y, Start and Back, the
 * shoulders and triggers; in a match a move stick on the left half and a look stick on the right half
 * (each one appears where the finger lands), the triggers (RT fires, LT is the alternate fire and the
 * shield), A B X Y, the shoulders, the stick clicks (R3 locks on), a small D-pad, Start (the menu) and
 * Back (the scores). Nothing is drawn and no touch is taken while the engine has no on-screen pad in use
 * (not started yet, or a real pad took player one's port). The "pad" key at the bottom hides and shows it.
 *
 * A real pad's input hides the whole overlay; a touch on the screen shows it again, and does nothing else.
 */
final class PadOverlayView extends View {
    // SDL_GameControllerButton values.
    private static final int A = 0, B = 1, X = 2, Y = 3, BACK = 4, START = 6, LSTICK = 7, RSTICK = 8,
            LSHOULDER = 9, RSHOULDER = 10, DUP = 11, DDOWN = 12, DLEFT = 13, DRIGHT = 14;
    // SDL_GameControllerAxis values.
    private static final int AX_LEFTX = 0, AX_LEFTY = 1, AX_RIGHTX = 2, AX_RIGHTY = 3, AX_LT = 4, AX_RT = 5;
    // nativeTouchPadState bits.
    private static final int ST_ATTACHED = 1, ST_IN_USE = 2, ST_MENU = 4;
    // The stick bindings' DeadZone (DefUser.ini, JoyX..JoyV): a stick's first movement starts past it.
    private static final float BINDING_DEAD_ZONE = 0.3f;
    private static final long POLL_MS = 100L;

    /** A round key: a pad button, or a trigger (axis >= 0). */
    private static final class Key {
        final int button, axis, color;
        final String label;
        float cx, cy, r;
        boolean down;
        Key(int button, int axis, String label, int color) { this.button = button; this.axis = axis; this.label = label; this.color = color; }
        boolean hit(float x, float y) { float dx = x - cx, dy = y - cy; return dx * dx + dy * dy <= (r * 1.25f) * (r * 1.25f); }
    }

    /** A stick that appears where the finger lands inside its zone and follows it past its rim. */
    private static final class Stick {
        final int axisX, axisY;
        float left, top, right, bottom, radius, restX, restY;
        float baseX, baseY, knobX, knobY;
        int pointer = -1;
        Stick(int axisX, int axisY) { this.axisX = axisX; this.axisY = axisY; }
        boolean inZone(float x, float y) { return x >= left && x < right && y >= top && y < bottom; }
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final ArrayList<Key> menuKeys = new ArrayList<Key>();
    private final ArrayList<Key> gameKeys = new ArrayList<Key>();
    private final Stick moveStick = new Stick(AX_LEFTX, AX_LEFTY);
    private final Stick lookStick = new Stick(AX_RIGHTX, AX_RIGHTY);
    private final Key toggle = new Key(-1, -1, "pad", 0xFFFFFFFF);
    private final SparseArray<Object> pointers = new SparseArray<Object>();

    private int state = 0;
    private boolean hidden = false;
    // Hidden by a real pad's input until the screen is touched; revealing: the touch that showed it, until it lifts.
    private boolean padHidden = false, revealing = false;

    PadOverlayView(Context ctx) {
        super(ctx);
        setFocusable(false);
        setWillNotDraw(false);
        stroke.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
    }

    // ---- the engine's state ---------------------------------------------------------------------

    /** The engine's state is read while the overlay can be seen; in the background nothing runs. */
    @Override
    public void onVisibilityAggregated(boolean isVisible) {
        super.onVisibilityAggregated(isVisible);
        removeCallbacks(poll);
        if (isVisible) postDelayed(poll, POLL_MS);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            int s;
            try { s = GameActivity.nativeTouchPadState(); } catch (Throwable t) { s = 0; }
            if (s != state) {
                boolean layoutChanged = (s & (ST_MENU | ST_IN_USE)) != (state & (ST_MENU | ST_IN_USE));
                state = s;
                if (layoutChanged) releaseAll();
                invalidate();
            }
            postDelayed(this, POLL_MS);
        }
    };

    private boolean active() { return (state & ST_ATTACHED) != 0 && (state & ST_IN_USE) != 0; }

    /** A real pad's button or stick (GameActivity): the overlay hides until the screen is touched. */
    void onRealPadInput() {
        if (padHidden) return;
        padHidden = true;
        releaseAll();
        invalidate();
    }
    private boolean menu() { return (state & ST_MENU) != 0; }
    private ArrayList<Key> keys() { return menu() ? menuKeys : gameKeys; }

    // ---- layout ---------------------------------------------------------------------------------

    private static Key key(ArrayList<Key> list, int button, int axis, String label, int color, float cx, float cy, float r) {
        Key k = new Key(button, axis, label, color);
        k.cx = cx; k.cy = cy; k.r = r;
        list.add(k);
        return k;
    }

    private static void diamond(ArrayList<Key> list, float cx, float cy, float d, float r) {
        key(list, A, -1, "A", 0xFF5FB34A, cx, cy + d, r);
        key(list, B, -1, "B", 0xFFD9473F, cx + d, cy, r);
        key(list, X, -1, "X", 0xFF3C7BD9, cx - d, cy, r);
        key(list, Y, -1, "Y", 0xFFE5C13A, cx, cy - d, r);
    }

    private static void dpad(ArrayList<Key> list, float cx, float cy, float d, float r) {
        key(list, DUP, -1, "▲", 0xFFC8CCD2, cx, cy - d, r);
        key(list, DDOWN, -1, "▼", 0xFFC8CCD2, cx, cy + d, r);
        key(list, DLEFT, -1, "◀", 0xFFC8CCD2, cx - d, cy, r);
        key(list, DRIGHT, -1, "▶", 0xFFC8CCD2, cx + d, cy, r);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        releaseAll();
        menuKeys.clear();
        gameKeys.clear();
        float s = Math.min(w, h), r = s * 0.065f, pad = s * 0.03f;
        int gray = 0xFFC8CCD2;

        // The menus.
        float mr = r * 1.15f;
        dpad(menuKeys, pad + 4.2f * mr, h * 0.58f, 2.2f * mr, mr);
        diamond(menuKeys, w - pad - 4.2f * mr, h * 0.58f, 2.2f * mr, mr);
        key(menuKeys, BACK, -1, "BACK", gray, w * 0.5f - 2.0f * r, pad + 1.2f * r, r);
        key(menuKeys, START, -1, "START", gray, w * 0.5f + 2.0f * r, pad + 1.2f * r, r);
        key(menuKeys, -1, AX_LT, "LT", gray, pad + 1.2f * r, pad + 1.2f * r, r);
        key(menuKeys, LSHOULDER, -1, "LB", gray, pad + 3.8f * r, pad + 1.2f * r, r);
        key(menuKeys, RSHOULDER, -1, "RB", gray, w - pad - 3.8f * r, pad + 1.2f * r, r);
        key(menuKeys, -1, AX_RT, "RT", gray, w - pad - 1.2f * r, pad + 1.2f * r, r);

        // A match.
        float fx = w - pad - 3.3f * r, fy = h - pad - 3.3f * r, d = 2.15f * r;
        diamond(gameKeys, fx, fy, d, r);
        key(gameKeys, -1, AX_RT, "RT", 0xFFE06A2C, w - pad - 1.5f * r, fy - d - 3.4f * r, r * 1.35f);
        key(gameKeys, -1, AX_LT, "LT", gray, w - pad - 4.7f * r, fy - d - 3.4f * r, r * 1.15f);
        key(gameKeys, RSHOULDER, -1, "RB", gray, w - pad - 1.1f * r, pad + 1.1f * r, r * 0.9f);
        key(gameKeys, LSHOULDER, -1, "LB", gray, w - pad - 3.5f * r, pad + 1.1f * r, r * 0.9f);
        key(gameKeys, RSTICK, -1, "R3", gray, fx - d - 2.3f * r, fy + d, r * 0.85f);
        key(gameKeys, LSTICK, -1, "L3", gray, w * 0.38f, h - pad - r, r * 0.85f);
        key(gameKeys, BACK, -1, "BACK", gray, w * 0.5f - 1.8f * r, pad + 0.9f * r, r * 0.8f);
        key(gameKeys, START, -1, "START", gray, w * 0.5f + 1.8f * r, pad + 0.9f * r, r * 0.8f);
        dpad(gameKeys, pad + 2.3f * r, pad + 2.3f * r, 1.5f * r, r * 0.7f);

        moveStick.left = 0; moveStick.right = w * 0.45f; moveStick.top = h * 0.30f; moveStick.bottom = h;
        moveStick.radius = r * 2.4f;
        moveStick.restX = pad + 3.6f * r; moveStick.restY = h - pad - 3.6f * r;
        lookStick.left = w * 0.45f; lookStick.right = w; lookStick.top = 0; lookStick.bottom = h;
        lookStick.radius = r * 2.0f;
        lookStick.restX = w * 0.66f; lookStick.restY = h * 0.62f;

        toggle.cx = w * 0.5f; toggle.cy = h - pad - 0.7f * r; toggle.r = r * 0.6f;
    }

    // ---- drawing --------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!active() || padHidden) return;
        drawKey(canvas, toggle, hidden ? 0x30 : 0x50);
        if (hidden) return;
        for (Key k : keys()) drawKey(canvas, k, 0xFF);
        if (!menu()) {
            drawStick(canvas, moveStick);
            drawStick(canvas, lookStick);
        }
    }

    private void drawKey(Canvas canvas, Key k, int alpha) {
        int rgb = k.color & 0xFFFFFF;
        fill.setColor(((k.down ? 0x90 : 0x26) * alpha / 0xFF) << 24 | rgb);
        canvas.drawCircle(k.cx, k.cy, k.r, fill);
        stroke.setStrokeWidth(Math.max(2f, k.r * 0.06f));
        stroke.setColor((0x90 * alpha / 0xFF) << 24 | rgb);
        canvas.drawCircle(k.cx, k.cy, k.r, stroke);
        text.setColor((0xC0 * alpha / 0xFF) << 24 | 0xFFFFFF);
        text.setTextSize(k.label.length() > 2 ? k.r * 0.5f : k.r * 0.8f);
        canvas.drawText(k.label, k.cx, k.cy - (text.descent() + text.ascent()) / 2f, text);
    }

    private void drawStick(Canvas canvas, Stick st) {
        boolean held = st.pointer >= 0;
        float bx = held ? st.baseX : st.restX, by = held ? st.baseY : st.restY;
        stroke.setStrokeWidth(Math.max(2f, st.radius * 0.03f));
        stroke.setColor(held ? 0x90FFFFFF : 0x40FFFFFF);
        canvas.drawCircle(bx, by, st.radius, stroke);
        fill.setColor(held ? 0x80FFFFFF : 0x30FFFFFF);
        canvas.drawCircle(held ? st.knobX : bx, held ? st.knobY : by, st.radius * 0.38f, fill);
    }

    // ---- touches --------------------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        int action = ev.getActionMasked(), index = ev.getActionIndex();
        if (revealing) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) revealing = false;
            return true;
        }
        if (!active()) {
            releaseAll();
            return false;
        }
        if (padHidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                padHidden = false;
                revealing = true;
                invalidate();
            }
            return true;
        }
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                press(ev.getPointerId(index), ev.getX(index), ev.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < ev.getPointerCount(); i++) move(ev.getPointerId(i), ev.getX(i), ev.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                release(ev.getPointerId(index));
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
        }
        invalidate();
        return true;
    }

    private void press(int id, float x, float y) {
        if (toggle.hit(x, y)) {
            hidden = !hidden;
            releaseAll();
            return;
        }
        if (hidden) return;
        for (Key k : keys()) {
            if (k.hit(x, y)) {
                pointers.put(id, k);
                setKey(k, true);
                return;
            }
        }
        if (menu()) return;
        Stick st = moveStick.inZone(x, y) && moveStick.pointer < 0 ? moveStick
                : lookStick.inZone(x, y) && lookStick.pointer < 0 ? lookStick : null;
        if (st == null) return;
        st.pointer = id;
        st.baseX = st.knobX = x;
        st.baseY = st.knobY = y;
        pointers.put(id, st);
    }

    private void move(int id, float x, float y) {
        Object o = pointers.get(id);
        if (o instanceof Stick) {
            moveStick((Stick) o, x, y);
        } else if (o instanceof Key && ((Key) o).button >= DUP && ((Key) o).button <= DRIGHT) {
            // A finger sliding over the D-pad moves to the direction under it.
            for (Key k : keys()) {
                if (k != o && k.button >= DUP && k.button <= DRIGHT && k.hit(x, y)) {
                    setKey((Key) o, false);
                    setKey(k, true);
                    pointers.put(id, k);
                    break;
                }
            }
        }
    }

    private void moveStick(Stick st, float x, float y) {
        float dx = x - st.baseX, dy = y - st.baseY;
        float len = (float) Math.hypot(dx, dy);
        if (len > st.radius) {
            // The base follows the finger, so pulling back always reverses at once.
            float k = (len - st.radius) / len;
            st.baseX += dx * k;
            st.baseY += dy * k;
            dx = x - st.baseX;
            dy = y - st.baseY;
            len = st.radius;
        }
        st.knobX = x;
        st.knobY = y;
        float m = len / st.radius, vx = 0f, vy = 0f;
        if (m > 0.04f) {
            float out = BINDING_DEAD_ZONE + (1f - BINDING_DEAD_ZONE) * m;
            vx = dx / len * out;
            vy = dy / len * out;
        }
        GameActivity.nativeTouchPadAxis(st.axisX, vx);
        GameActivity.nativeTouchPadAxis(st.axisY, vy);
    }

    private void release(int id) {
        Object o = pointers.get(id);
        pointers.remove(id);
        if (o instanceof Key) {
            setKey((Key) o, false);
        } else if (o instanceof Stick) {
            Stick st = (Stick) o;
            st.pointer = -1;
            GameActivity.nativeTouchPadAxis(st.axisX, 0f);
            GameActivity.nativeTouchPadAxis(st.axisY, 0f);
        }
    }

    private void setKey(Key k, boolean down) {
        k.down = down;
        if (k.axis >= 0) GameActivity.nativeTouchPadAxis(k.axis, down ? 1f : 0f);
        else if (k.button >= 0) GameActivity.nativeTouchPadButton(k.button, down);
    }

    /** Lets go of everything (a layout change, a cancelled gesture, the pad hidden). */
    private void releaseAll() {
        try {
            for (int i = pointers.size() - 1; i >= 0; i--) release(pointers.keyAt(i));
            for (Key k : menuKeys) if (k.down) setKey(k, false);
            for (Key k : gameKeys) if (k.down) setKey(k, false);
        } catch (Throwable ignored) {
            // The engine's library is not loaded yet: nothing is held there either.
        }
        pointers.clear();
        moveStick.pointer = lookStick.pointer = -1;
    }
}
