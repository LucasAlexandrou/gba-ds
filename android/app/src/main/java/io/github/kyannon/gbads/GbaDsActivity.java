package io.github.kyannon.gbads;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
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

    @Override
    protected String[] getArguments() {
        String rom = getIntent() != null ? getIntent().getStringExtra("rom") : null;
        if (rom != null && !rom.isEmpty()) {
            Log.i(TAG, "rom from intent: " + rom);
            return new String[] { rom };
        }
        return new String[0];
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

    @Override
    protected void onDestroy() {
        if (second != null) {
            second.dismiss();
            second = null;
        }
        boolean finishing = isFinishing();
        super.onDestroy();
        /* the native side keeps its state in statics: start fresh next time */
        if (finishing) System.exit(0);
    }
}
