package io.github.kyannon.gbads;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;

/**
 * The app icon. Asks for "All files access" once (so ROMs can be read from any
 * folder), then starts the game on the main display, where SDL draws; the
 * game activity puts its second screen on the other display.
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "GBADS";
    private boolean askedForFiles;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) askedForFiles = state.getBoolean("askedForFiles");
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putBoolean("askedForFiles", askedForFiles);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            if (!askedForFiles) {
                askedForFiles = true;
                Toast.makeText(this, R.string.need_files, Toast.LENGTH_LONG).show();
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + getPackageName())));
                    return;
                } catch (ActivityNotFoundException e) {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                        return;
                    } catch (ActivityNotFoundException e2) {
                        Log.w(TAG, "no settings screen for all files access");
                    }
                }
            } else {
                Log.w(TAG, "all files access not granted; ROM folders may be unreadable");
            }
        }
        startGame();
    }

    private void startGame() {
        Intent game = new Intent(this, GbaDsActivity.class);
        if (getIntent() != null && getIntent().getExtras() != null) game.putExtras(getIntent().getExtras());
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(Display.DEFAULT_DISPLAY);
        try {
            startActivity(game, options.toBundle());
        } catch (SecurityException | IllegalArgumentException e) {
            Log.w(TAG, "could not pick the display: " + e);
            startActivity(game);
        }
        finish();
    }
}
