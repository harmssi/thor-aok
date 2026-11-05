package org.openaok.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextPaint;
import android.text.TextUtils;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.HashMap;
import java.util.Map;

/**
 * Command deck for the AYN Thor's bottom screen (display 4, 1240x1080).
 *
 * Stockpiles, population and age · mini-map (tap or drag moves the view) · the
 * selection with its hit points and queue · the quick buttons (idle villager,
 * idle soldier, Town Center, next age, menu) · the selection's command grid as
 * big tiles with the icons from the player's own game files. Everything shown
 * is the engine's snapshot (src/thor/Thor.cpp); every press goes back as a
 * command the engine runs through its own panel code.
 *
 * Laid out on a 1240x1080 grid and scaled to the view.
 */
public class ThorDeck extends View {
    private static final int W = 1240, H = 1080;
    private static final long POLL_MS = 50, HOLD_MS = 420;

    // Colours: dark panels, thin lines, amber for what can be pressed.
    private static final int BG = 0xFF0E0D0B, PANEL = 0xFF1A1814, PANEL_HI = 0xFF26221B, LINE = 0xFF3A3328,
            AMBER = 0xFFE8B04A, TEXT = 0xFFEDE6D6, DIM = 0xFF8C8372, RED = 0xFFD9573F, GREEN = 0xFF6FBF4A;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final int[] snap = new int[2048];
    private int snapLen;
    private final int[] mapPx = new int[512 * 256];
    private Bitmap mapBitmap;
    private int mapSerial = -1;
    private final Map<Long, Bitmap> icons = new HashMap<>();
    private final int[] iconBuf = new int[256 * 256];
    private final int[] iconWh = new int[2];
    private final Map<Integer, String> words = new HashMap<>();
    private boolean running, inGame;

    private final String[] labels = {"", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};
    private String ageName = "", nextAge = "", selName = "", help = "";
    private long helpUntil;
    private int pressedTile = -1, pressedButton = -1;
    private long pressedAt;
    private boolean held, mapDrag;
    private long lastMapPush;

    // Layout, on the 1240x1080 grid
    private float s = 1f;
    private final RectF header = new RectF(0, 0, W, 92);
    private final RectF mapPanel = new RectF(16, 104, 656, 440);
    private final RectF mapArea = new RectF(24, 112, 648, 424);
    private final RectF selPanel = new RectF(672, 104, 1224, 440);
    private final RectF[] buttons = new RectF[6];
    private final RectF grid = new RectF(16, 548, 1224, 1064);
    private final RectF[] tiles = new RectF[15];
    private final RectF[] queueRects = new RectF[15];
    private int[] queueSlot = new int[15];

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bmp = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Path path = new Path();

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (!running) return;
            refresh();
            // A tile held long enough shows its help instead of acting.
            if (pressedTile >= 0 && !held && SystemClock.uptimeMillis() - pressedAt > HOLD_MS) {
                held = true;
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
                showHelp(pressedTile);
            }
            invalidate();
            handler.postDelayed(this, POLL_MS);
        }
    };

    public ThorDeck(Context context) {
        super(context);
        setBackgroundColor(BG);
        stroke.setStyle(Paint.Style.STROKE);
        text.setTypeface(Typeface.create("sans-serif-condensed", Typeface.NORMAL));
        float bw = (W - 32 - 5 * 12) / 6f;
        for (int i = 0; i < buttons.length; i++) {
            float x = 16 + i * (bw + 12);
            buttons[i] = new RectF(x, 452, x + bw, 536);
        }
        float tw = (grid.width() - 4 * 10) / 5f, th = (grid.height() - 2 * 10) / 3f;
        for (int i = 0; i < 15; i++) {
            float x = grid.left + (i % 5) * (tw + 10), y = grid.top + (i / 5) * (th + 10);
            tiles[i] = new RectF(x, y, x + tw, y + th);
        }
        for (int i = 0; i < 15; i++) queueRects[i] = new RectF();
    }

    void start() {
        if (running) return;
        running = true;
        handler.post(poll);
    }

    void stop() {
        running = false;
        handler.removeCallbacks(poll);
    }

    // --- data ---------------------------------------------------------------------------

    private int h(int field) {
        return snap[1 + field];
    }

    private void refresh() {
        int n;
        try {
            n = ThorBridge.snapshot(snap);
        } catch (UnsatisfiedLinkError e) {
            n = 0;
        }
        snapLen = n;
        inGame = n > ThorBridge.HEADER && snap[0] < 1500 && h(ThorBridge.H_IN_GAME) == 1;
        if (!inGame) return;
        if (h(ThorBridge.H_MAP_SERIAL) != mapSerial) {
            mapSerial = h(ThorBridge.H_MAP_SERIAL);
            int w = h(ThorBridge.H_MAP_W), hh = h(ThorBridge.H_MAP_H);
            if (w > 0 && hh > 0 && w * hh <= mapPx.length && ThorBridge.minimap(mapPx) == w * hh) {
                if (mapBitmap == null || mapBitmap.getWidth() != w || mapBitmap.getHeight() != hh) {
                    mapBitmap = Bitmap.createBitmap(w, hh, Bitmap.Config.ARGB_8888);
                }
                mapBitmap.setPixels(mapPx, 0, w, 0, 0, w, hh);
            }
        }
        ageName = ThorBridge.text(ThorBridge.TEXT_AGE, 0);
        nextAge = ThorBridge.text(ThorBridge.TEXT_NEXT_AGE, 0);
        selName = ThorBridge.text(ThorBridge.TEXT_NAME, 0);
        int count = Math.min(15, h(ThorBridge.H_COMMANDS));
        for (int i = 0; i < count; i++) labels[i] = ThorBridge.text(ThorBridge.TEXT_LABEL, i);
    }

    private Bitmap icon(int slp, int frame) {
        if (slp <= 0 || frame < 0) return null;
        long key = ((long) slp << 20) | frame;
        if (icons.containsKey(key)) return icons.get(key);
        Bitmap b = null;
        int n = ThorBridge.icon(slp, frame, iconBuf, iconWh);
        if (n > 0) {
            b = Bitmap.createBitmap(iconWh[0], iconWh[1], Bitmap.Config.ARGB_8888);
            b.setPixels(iconBuf, 0, iconWh[0], 0, 0, iconWh[0], iconWh[1]);
        }
        icons.put(key, b);
        return b;
    }

    private String word(int id) {
        if (id < 0) return "";
        String w = words.get(id);
        if (w == null) {
            w = ThorBridge.text(ThorBridge.TEXT_STRING, id);
            words.put(id, w);
        }
        return w;
    }

    private int commandBase(int i) {
        return 1 + ThorBridge.HEADER + i * ThorBridge.COMMAND;
    }

    /** The command (index into the snapshot) on grid slot 1..15, or -1. */
    private int commandAt(int slot) {
        int n = h(ThorBridge.H_COMMANDS);
        for (int i = 0; i < n; i++) {
            if (snap[commandBase(i)] == slot) return i;
        }
        return -1;
    }

    private void showHelp(int tile) {
        int c = commandAt(tile + 1);
        if (c < 0) return;
        String full = ThorBridge.text(ThorBridge.TEXT_HELP, c);
        help = full.replace("<b>", "").replace("<i>", "").replace("</b>", "").replace("</i>", "")
                .replace('\n', ' ').trim();
        helpUntil = SystemClock.uptimeMillis() + 6000;
    }

    // --- drawing ------------------------------------------------------------------------

    @Override
    protected void onSizeChanged(int w, int hh, int ow, int oh) {
        s = Math.min(w / (float) W, hh / (float) H);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        canvas.save();
        canvas.scale(s, s);
        if (!inGame) {
            drawIdle(canvas);
        } else {
            drawHeader(canvas);
            drawMinimap(canvas);
            drawSelection(canvas);
            drawButtons(canvas);
            drawGrid(canvas);
        }
        canvas.restore();
    }

    private void panel(Canvas c, RectF r, int color) {
        fill.setShader(null);
        fill.setColor(color);
        c.drawRoundRect(r, 10, 10, fill);
        stroke.setColor(LINE);
        stroke.setStrokeWidth(2);
        c.drawRoundRect(r, 10, 10, stroke);
    }

    private void label(Canvas c, String t, float x, float y, float size, int color, Paint.Align align, boolean bold) {
        text.setTextSize(size);
        text.setColor(color);
        text.setTextAlign(align);
        text.setFakeBoldText(bold);
        c.drawText(t, x, y, text);
    }

    private void drawIdle(Canvas c) {
        label(c, "THOR AoK", W / 2f, H / 2f - 40, 96, AMBER, Paint.Align.CENTER, true);
        label(c, getContext().getString(R.string.deck_idle), W / 2f, H / 2f + 40, 36, DIM, Paint.Align.CENTER, false);
    }

    private void drawHeader(Canvas c) {
        fill.setShader(new LinearGradient(0, 0, 0, header.bottom, PANEL_HI, BG, Shader.TileMode.CLAMP));
        c.drawRect(header, fill);
        fill.setShader(null);
        String[] names = {
                getContext().getString(R.string.deck_wood), getContext().getString(R.string.deck_food),
                getContext().getString(R.string.deck_gold), getContext().getString(R.string.deck_stone)};
        int[] colors = {0xFF9C6B3A, 0xFFD9573F, 0xFFE8C14A, 0xFFAFAFA8};
        int[] values = {h(ThorBridge.H_WOOD), h(ThorBridge.H_FOOD), h(ThorBridge.H_GOLD), h(ThorBridge.H_STONE)};
        float x = 24;
        for (int i = 0; i < 4; i++) {
            fill.setColor(colors[i]);
            c.drawCircle(x + 14, 46, 12, fill);
            label(c, names[i], x + 36, 34, 22, DIM, Paint.Align.LEFT, false);
            label(c, String.valueOf(values[i]), x + 36, 74, 40, TEXT, Paint.Align.LEFT, true);
            x += 196;
        }
        int pop = h(ThorBridge.H_POP), cap = h(ThorBridge.H_POP_CAP);
        label(c, getContext().getString(R.string.deck_pop), x + 8, 34, 22, DIM, Paint.Align.LEFT, false);
        label(c, pop + "/" + cap, x + 8, 74, 40, pop >= cap ? RED : TEXT, Paint.Align.LEFT, true);
        label(c, ageName.toUpperCase(), W - 24, 60, 38, AMBER, Paint.Align.RIGHT, true);
        stroke.setColor(LINE);
        stroke.setStrokeWidth(2);
        c.drawLine(0, header.bottom, W, header.bottom, stroke);
    }

    private void drawMinimap(Canvas c) {
        panel(c, mapPanel, PANEL);
        if (mapBitmap != null) {
            c.drawBitmap(mapBitmap, null, mapArea, bmp);
            int w = h(ThorBridge.H_MAP_W), hh = h(ThorBridge.H_MAP_H);
            if (w > 0 && hh > 0) {
                float sx = mapArea.width() / w, sy = mapArea.height() / hh;
                path.reset();
                for (int i = 0; i < 4; i++) {
                    float px = mapArea.left + h(ThorBridge.H_VIEW + i * 2) * sx;
                    float py = mapArea.top + h(ThorBridge.H_VIEW + i * 2 + 1) * sy;
                    if (i == 0) path.moveTo(px, py);
                    else path.lineTo(px, py);
                }
                path.close();
                c.save();
                c.clipRect(mapArea);
                stroke.setColor(Color.WHITE);
                stroke.setStrokeWidth(2.5f);
                c.drawPath(path, stroke);
                c.restore();
            }
        }
    }

    private void drawSelection(Canvas c) {
        panel(c, selPanel, PANEL);
        float l = selPanel.left + 20, t = selPanel.top + 20;
        if (helpUntil > SystemClock.uptimeMillis() && !help.isEmpty()) {
            text.setTextSize(26);
            drawWrapped(c, help, l, t + 26, selPanel.right - 20, 32, 9, TEXT);
            return;
        }
        if (h(ThorBridge.H_SELECTED) == 0 && h(ThorBridge.H_SEL_UNIT) < 0) {
            label(c, getContext().getString(R.string.deck_nothing), selPanel.centerX(), selPanel.centerY(), 28, DIM,
                    Paint.Align.CENTER, false);
            return;
        }
        Bitmap b = icon(h(ThorBridge.H_SEL_SLP), h(ThorBridge.H_SEL_FRAME));
        RectF ir = new RectF(l, t, l + 128, t + 128);
        if (b != null) c.drawBitmap(b, null, ir, bmp);
        stroke.setColor(LINE);
        c.drawRect(ir, stroke);
        int selected = h(ThorBridge.H_SELECTED);
        String name = selected > 1 ? selName + "  ×" + selected : selName;
        label(c, TextUtils.ellipsize(name, measure(34), selPanel.right - ir.right - 40, TextUtils.TruncateAt.END)
                .toString(), ir.right + 20, t + 40, 34, TEXT, Paint.Align.LEFT, true);
        int hp = h(ThorBridge.H_HP), max = h(ThorBridge.H_MAX_HP);
        if (max > 0) {
            RectF bar = new RectF(ir.right + 20, t + 62, selPanel.right - 20, t + 82);
            fill.setColor(0xFF3A1A14);
            c.drawRect(bar, fill);
            fill.setColor(GREEN);
            c.drawRect(bar.left, bar.top, bar.left + bar.width() * Math.max(0, Math.min(hp, max)) / max, bar.bottom, fill);
            label(c, hp + " / " + max, ir.right + 20, t + 116, 26, DIM, Paint.Align.LEFT, false);
        }
        // The queue: each item a small tile, the first with its progress; a tap takes it back.
        int queued = h(ThorBridge.H_QUEUE);
        int base = 1 + ThorBridge.HEADER + h(ThorBridge.H_COMMANDS) * ThorBridge.COMMAND;
        float qx = l, qy = t + 150, qs = 76;
        for (int i = 0; i < 15; i++) queueRects[i].setEmpty();
        for (int i = 0; i < queued && i < 15; i++) {
            int o = base + i * ThorBridge.QUEUE;
            RectF r = queueRects[i];
            r.set(qx, qy, qx + qs, qy + qs);
            queueSlot[i] = snap[o];
            panel(c, r, PANEL_HI);
            Bitmap qb = icon(snap[o + 3], snap[o + 4]);
            if (qb != null) c.drawBitmap(qb, null, new RectF(r.left + 4, r.top + 4, r.right - 4, r.bottom - 4), bmp);
            if (snap[o + 2] > 1) label(c, "×" + snap[o + 2], r.left + 8, r.top + 28, 24, AMBER, Paint.Align.LEFT, true);
            if (i == 0 && h(ThorBridge.H_QUEUE_TOTAL) > 0) {
                float f = Math.min(1f, h(ThorBridge.H_QUEUE_DONE) / (float) h(ThorBridge.H_QUEUE_TOTAL));
                fill.setColor(GREEN);
                c.drawRect(r.left, r.bottom + 4, r.left + r.width() * f, r.bottom + 10, fill);
            }
            qx += qs + 10;
            if (qx + qs > selPanel.right - 10) {
                qx = l;
                qy += qs + 18;
            }
        }
    }

    private TextPaint measure(float size) {
        text.setTextSize(size);
        return text;
    }

    private void drawWrapped(Canvas c, String t, float x, float y, float right, float lineH, int maxLines, int color) {
        text.setColor(color);
        text.setTextAlign(Paint.Align.LEFT);
        text.setFakeBoldText(false);
        String[] wordsIn = t.split(" ");
        StringBuilder line = new StringBuilder();
        int lines = 0;
        for (String w : wordsIn) {
            String tryLine = line.length() == 0 ? w : line + " " + w;
            if (text.measureText(tryLine) > right - x && line.length() > 0) {
                c.drawText(line.toString(), x, y, text);
                y += lineH;
                if (++lines >= maxLines) return;
                line = new StringBuilder(w);
            } else {
                line = new StringBuilder(tryLine);
            }
        }
        if (line.length() > 0) c.drawText(line.toString(), x, y, text);
    }

    private String buttonLabel(int i) {
        switch (i) {
            case 0: return getContext().getString(R.string.deck_idle_villager);
            case 1: return getContext().getString(R.string.deck_idle_military);
            case 2: return getContext().getString(R.string.deck_town_center);
            case 3: return nextAge.isEmpty() ? getContext().getString(R.string.deck_age_up) : nextAge;
            case 4: return getContext().getString(R.string.deck_delete);
            default: return getContext().getString(R.string.deck_menu);
        }
    }

    private void drawButtons(Canvas c) {
        boolean ageRunning = (h(ThorBridge.H_FLAGS) & ThorBridge.F_AGE_RESEARCHING) != 0;
        for (int i = 0; i < buttons.length; i++) {
            RectF r = buttons[i];
            boolean down = pressedButton == i;
            panel(c, r, down ? PANEL_HI : PANEL);
            int color = i == 3 ? (ageRunning ? GREEN : AMBER) : TEXT;
            if (i == 4) color = RED;
            String t = buttonLabel(i);
            if (i <= 1) {
                label(c, getContext().getString(R.string.deck_idle_caption).toUpperCase(), r.centerX(), r.top + 30, 18, DIM,
                        Paint.Align.CENTER, false);
                label(c, TextUtils.ellipsize(t, measure(26), r.width() - 16, TextUtils.TruncateAt.END).toString(),
                        r.centerX(), r.top + 66, 26, color, Paint.Align.CENTER, true);
            } else if (i == 3) {
                label(c, getContext().getString(R.string.deck_age_up).toUpperCase(), r.centerX(), r.top + 30, 18, DIM,
                        Paint.Align.CENTER, false);
                label(c, TextUtils.ellipsize(t, measure(26), r.width() - 16, TextUtils.TruncateAt.END).toString(),
                        r.centerX(), r.top + 66, 26, color, Paint.Align.CENTER, true);
            } else {
                label(c, TextUtils.ellipsize(t, measure(26), r.width() - 16, TextUtils.TruncateAt.END).toString(),
                        r.centerX(), r.centerY() + 10, 26, color, Paint.Align.CENTER, true);
            }
        }
    }

    private void drawGrid(Canvas c) {
        boolean placing = (h(ThorBridge.H_FLAGS) & ThorBridge.F_PLACING) != 0;
        for (int t = 0; t < 15; t++) {
            RectF r = tiles[t];
            int ci = commandAt(t + 1);
            if (ci < 0) {
                fill.setColor(0xFF15130F);
                c.drawRoundRect(r, 10, 10, fill);
                continue;
            }
            int o = commandBase(ci);
            int kind = snap[o + 1];
            boolean enabled = snap[o + 4] == 1, affordable = snap[o + 5] == 1;
            panel(c, r, pressedTile == t ? PANEL_HI : PANEL);
            String name = ci < labels.length ? labels[ci] : "";
            if (kind == ThorBridge.KIND_CAPTION) {
                label(c, name, r.centerX(), r.centerY() + 12, 32, DIM, Paint.Align.CENTER, true);
                continue;
            }
            Bitmap b = icon(snap[o + 2], snap[o + 3]);
            float side = Math.min(r.height() - 56, r.width() - 40);
            RectF ir = new RectF(r.centerX() - side / 2, r.top + 12, r.centerX() + side / 2, r.top + 12 + side);
            bmp.setAlpha(enabled ? 255 : 90);
            if (b != null) c.drawBitmap(b, null, ir, bmp);
            bmp.setAlpha(255);
            if (snap[o + 6] >= 0) label(c, String.valueOf(snap[o + 6]), ir.left + 6, ir.top + 28, 26, AMBER, Paint.Align.LEFT, true);
            int color = !enabled ? DIM : affordable ? TEXT : RED;
            label(c, TextUtils.ellipsize(name, measure(22), r.width() - 16, TextUtils.TruncateAt.END).toString(),
                    r.centerX(), r.bottom - 14, 22, color, Paint.Align.CENTER, false);
        }
        if (placing) {
            label(c, getContext().getString(R.string.deck_placing), grid.centerX(), grid.top - 6, 22, AMBER,
                    Paint.Align.CENTER, false);
        }
    }

    // --- touch --------------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX() / s, y = e.getY() / s;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressedTile = -1;
                pressedButton = -1;
                held = false;
                mapDrag = false;
                if (!inGame) return true;
                if (mapArea.contains(x, y)) {
                    mapDrag = true;
                    pushMap(x, y, true);
                    return true;
                }
                for (int i = 0; i < tiles.length; i++) {
                    if (tiles[i].contains(x, y) && commandAt(i + 1) >= 0) {
                        pressedTile = i;
                        pressedAt = SystemClock.uptimeMillis();
                    }
                }
                for (int i = 0; i < buttons.length; i++) {
                    if (buttons[i].contains(x, y)) pressedButton = i;
                }
                invalidate();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (mapDrag) pushMap(x, y, false);
                return true;
            case MotionEvent.ACTION_UP:
                if (mapDrag) {
                    pushMap(x, y, true);
                } else if (pressedTile >= 0 && tiles[pressedTile].contains(x, y) && !held) {
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                    ThorBridge.push(ThorBridge.CMD_COMMAND, pressedTile + 1, 0);
                    helpUntil = 0;
                } else if (pressedButton >= 0 && buttons[pressedButton].contains(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                    int[] cmd = {ThorBridge.CMD_IDLE_VILLAGER, ThorBridge.CMD_IDLE_MILITARY, ThorBridge.CMD_TOWN_CENTER,
                            ThorBridge.CMD_AGE_UP, ThorBridge.CMD_DELETE, ThorBridge.CMD_MENU};
                    ThorBridge.push(cmd[pressedButton], 0, 0);
                } else {
                    for (int i = 0; i < 15; i++) {
                        if (!queueRects[i].isEmpty() && queueRects[i].contains(x, y)) {
                            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                            ThorBridge.push(ThorBridge.CMD_CANCEL_QUEUE, queueSlot[i], 0);
                            break;
                        }
                    }
                }
                pressedTile = -1;
                pressedButton = -1;
                mapDrag = false;
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressedTile = -1;
                pressedButton = -1;
                mapDrag = false;
                return true;
            default:
                return true;
        }
    }

    private void pushMap(float x, float y, boolean now) {
        long t = SystemClock.uptimeMillis();
        if (!now && t - lastMapPush < 33) return;
        lastMapPush = t;
        float fx = (x - mapArea.left) / mapArea.width(), fy = (y - mapArea.top) / mapArea.height();
        fx = Math.max(0f, Math.min(0.9999f, fx));
        fy = Math.max(0f, Math.min(0.9999f, fy));
        ThorBridge.push(ThorBridge.CMD_MINIMAP, (int) (fx * 65536), (int) (fy * 65536));
    }
}
