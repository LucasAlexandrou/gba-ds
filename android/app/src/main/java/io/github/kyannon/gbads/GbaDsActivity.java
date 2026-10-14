package io.github.kyannon.gbads;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * The game. SDL draws on the display this activity is on; the other display
 * gets a {@link SecondScreen} (a Presentation) that the native code draws
 * into.
 *
 * Intent extra "rom": a ROM path to play (e.g. adb shell am start -n
 * io.github.kyannon.gbads/.LauncherActivity --es rom /storage/emulated/0/ROMs/gba/x.gba).
 */
public class GbaDsActivity extends SDLActivity {
    private static final String TAG = "GBADS";
    private SecondScreen second;

    @Override
    protected void onCreate(Bundle state) {
        /* before SDL starts the native thread: where config, states and the log go */
        File home = getExternalFilesDir(null);
        if (home == null) home = getFilesDir();
        home.mkdirs();
        try {
            Os.setenv("GBADS_HOME", home.getAbsolutePath(), true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv: " + e);
        }
        Log.i(TAG, "GBA DS " + BuildConfig.VERSION_NAME + ", data in " + home);
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (!mBrokenLibraries) openSecondScreen();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    private String currentRom; /* the game started with (null: the game list) */
    private Intent switchTo;   /* a frontend asked for another game: restart with it */

    @Override
    protected String[] getArguments() {
        String rom = getIntent() != null ? getIntent().getStringExtra("rom") : null;
        if (rom != null && !rom.isEmpty()) {
            Log.i(TAG, "rom from intent: " + rom);
            currentRom = rom;
            return new String[] { rom };
        }
        return new String[0];
    }

    /** Already running and started again (app icon, or a frontend with a game). */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String rom = intent.getStringExtra("rom");
        if (rom == null || rom.equals(currentRom)) return; /* just brought to the front */
        Log.i(TAG, "asked for another game (" + rom + "): closing this one first");
        switchTo = new Intent(this, LauncherActivity.class).putExtra("rom", rom).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        /* the game saves its resume point and returns from main; SDL then finishes this activity */
        SDLActivity.nativeSendQuit();
    }

    private void openSecondScreen() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        Display other = null;
        for (Display d : dm.getDisplays()) {
            Point size = new Point();
            d.getRealSize(size);
            Log.i(TAG, "display " + d.getDisplayId() + " \"" + d.getName() + "\" " + size.x + "x" + size.y
                    + " rotation " + d.getRotation() + " flags 0x" + Integer.toHexString(d.getFlags())
                    + (d.getDisplayId() == mine ? " (game activity)" : ""));
            if (d.getDisplayId() != mine && other == null) other = d;
        }
        if (other == null) {
            Log.i(TAG, "only one display: no second screen");
            return;
        }
        Point size = new Point();
        other.getRealSize(size);
        SecondScreen.nativeSetDisplay(size.x, size.y);
        second = new SecondScreen(this, other);
        try {
            second.show();
            Log.i(TAG, "second screen on display " + other.getDisplayId() + " (" + size.x + "x" + size.y + ")");
        } catch (WindowManager.InvalidDisplayException e) {
            Log.e(TAG, "second screen: " + e);
            SecondScreen.nativeSetDisplay(0, 0);
            second = null;
        }
    }

    /* ------------------------------------------------------------ ROM folder picker */

    private static final int PICK_FOLDER = 4711;

    static native void nativeFolderPicked(String path);

    /** Called by the native side (game thread): opens Android's folder picker. */
    public void pickRomFolder() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(i, PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no folder picker: " + e);
                nativeFolderPicked("");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String path = "";
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            path = treeUriToPath(data.getData());
            Log.i(TAG, "folder picked: " + data.getData() + " -> " + path);
        }
        nativeFolderPicked(path);
    }

    /** A picked folder (tree URI) as a plain path (readable with All files access). */
    private static String treeUriToPath(Uri uri) {
        try {
            String path = LauncherActivity.documentIdToPath(DocumentsContract.getTreeDocumentId(uri));
            return path != null ? path : "";
        } catch (IllegalArgumentException e) {
            return "";
        }
    }

    @Override
    protected void onDestroy() {
        if (second != null) {
            second.dismiss();
            second = null;
        }
        boolean finishing = isFinishing();
        super.onDestroy();
        /* the native side keeps its state in statics: start fresh next time
         * (with the game a frontend asked for, if that's why we closed) */
        if (finishing) {
            if (switchTo != null) startActivity(switchTo);
            System.exit(0);
        }
    }
}
