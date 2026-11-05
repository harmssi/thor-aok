package org.openaok.game;

/**
 * JNI bridge to the engine's deck module (src/thor/Thor.cpp, in libmain.so).
 *
 * The deck reads a snapshot the engine writes once a frame and sends commands
 * back; the engine runs each on its own thread through the code a click on
 * the original panel runs.
 */
final class ThorBridge {
    private ThorBridge() {}

    // Commands (Thor::Command)
    static final int CMD_COMMAND = 1;      // a = command grid slot 1..15
    static final int CMD_MINIMAP = 2;      // a, b = point of the mini-map picture, 0..65535
    static final int CMD_IDLE_VILLAGER = 3;
    static final int CMD_IDLE_MILITARY = 4;
    static final int CMD_TOWN_CENTER = 5;
    static final int CMD_AGE_UP = 6;
    static final int CMD_MENU = 7;
    static final int CMD_CANCEL_QUEUE = 8; // a = queue slot
    static final int CMD_DELETE = 9;
    static final int CMD_OBJECTIVES = 10;
    static final int CMD_TECH_TREE = 11;

    // Snapshot header, after out[0] = its age in ms (Thor::Header)
    static final int H_VERSION = 0, H_IN_GAME = 1, H_WOOD = 2, H_FOOD = 3, H_GOLD = 4, H_STONE = 5, H_POP = 6,
            H_POP_CAP = 7, H_AGE = 8, H_FLAGS = 9, H_SELECTED = 10, H_HP = 11, H_MAX_HP = 12, H_SEL_SLP = 13,
            H_SEL_FRAME = 14, H_SEL_UNIT = 15, H_QUEUE_DONE = 16, H_QUEUE_TOTAL = 17, H_COMMANDS = 18, H_QUEUE = 19,
            H_MAP_SERIAL = 20, H_MAP_W = 21, H_MAP_H = 22, H_VIEW = 23, HEADER = 31;
    static final int COMMAND = 8, QUEUE = 5;
    static final int F_PLACING = 1, F_AGE_RESEARCHING = 2;
    // Command kinds (UIRenderer::CommandKind) the deck treats apart
    static final int KIND_CAPTION = 27;
    static final int TEXT_AGE = 0, TEXT_NEXT_AGE = 1, TEXT_NAME = 2, TEXT_HELP = 3, TEXT_STRING = 4, TEXT_LABEL = 5;

    static native void setDualScreen(boolean on);
    static native void pushCommand(int type, int a, int b);
    static native int snapshot(int[] out);
    static native int minimap(int[] out);
    static native int icon(int slp, int frame, int[] out, int[] wh);
    static native String text(int which, int id);

    static void push(int type, int a, int b) {
        try {
            pushCommand(type, a, b);
        } catch (UnsatisfiedLinkError ignored) {
            // engine not loaded
        }
    }
}
