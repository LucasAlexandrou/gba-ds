package io.github.kyannon.gbads;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The app icon, and what frontends start. Asks for "All files access" once
 * (so ROMs can be read from any folder), then starts the game on the main
 * display, where SDL draws; the game activity puts its second screen on the
 * other display.
 *
 * A game to play straight away (skipping the game list) can come as
 * - an intent extra "rom" (also "ROM", "path", "file"...) with a file path, or
 *   a file:// / content:// URI, e.g. ES-DE: %EXTRA_rom%=%ROM%
 * - the intent's data, e.g. ES-DE: %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%,
 *   or Daijisho: -a android.intent.action.VIEW -d {file.uri}
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
        String rom = romFromIntent(this, getIntent());
        if (rom != null) {
            Log.i(TAG, "game from the intent: " + rom);
            game.putExtra("rom", rom);
        }
        /* A task of its own (see taskAffinity in the manifest): started inside
         * this launcher's task, it would open on whatever display the launcher
         * was put on (GammaOS: often the bottom one), ignoring the display asked for. */
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Log.i(TAG, "launcher on display " + getWindowManager().getDefaultDisplay().getDisplayId()
                + ", starting the game on display " + Display.DEFAULT_DISPLAY);
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

    /* ------------------------------------------------------------ the ROM from an intent */

    /* the names frontends' templates use for the game (GammaOS's examples: bootPath) */
    private static final String[] ROM_EXTRAS = { "rom", "ROM", "Rom", "path", "PATH", "file", "FILE", "romPath", "GamePath",
            "bootPath", "BootPath", "uri", "URI", "romUri", "filePath", "file_path", "rom_path", "gamePath", "game" };

    /** Everything a launch request carries, for the log (frontends differ; this shows what one sent). */
    private static void logIntent(Intent in) {
        StringBuilder b = new StringBuilder("launched with action=").append(in.getAction())
                .append(" data=").append(in.getData()).append(" type=").append(in.getType())
                .append(" flags=0x").append(Integer.toHexString(in.getFlags()));
        if (in.getCategories() != null) b.append(" categories=").append(in.getCategories());
        if (in.getExtras() != null) {
            for (String k : in.getExtras().keySet()) b.append(" extra ").append(k).append('=').append(in.getExtras().get(k));
        }
        if (in.getClipData() != null && in.getClipData().getItemCount() > 0) b.append(" clip=").append(in.getClipData().getItemAt(0).getUri());
        Log.i(TAG, b.toString());
    }

    static String romFromIntent(Context c, Intent in) {
        if (in == null) return null;
        logIntent(in);
        for (String k : ROM_EXTRAS) {
            Object v = in.getExtras() != null ? in.getExtras().get(k) : null;
            if (v instanceof Uri) return fromUri(c, (Uri) v);
            if (v instanceof String && !((String) v).isEmpty()) {
                String str = (String) v;
                return str.startsWith("file:") || str.startsWith("content:") ? fromUri(c, Uri.parse(str)) : str;
            }
        }
        if (in.getData() != null) return fromUri(c, in.getData());
        /* some send the file as ClipData only */
        if (in.getClipData() != null && in.getClipData().getItemCount() > 0 && in.getClipData().getItemAt(0).getUri() != null) {
            return fromUri(c, in.getClipData().getItemAt(0).getUri());
        }
        return null;
    }

    private static String fromUri(Context c, Uri u) {
        if ("file".equals(u.getScheme())) return u.getPath();
        if (!"content".equals(u.getScheme())) return null;
        if ("com.android.externalstorage.documents".equals(u.getAuthority())) {
            /* with All files access, the plain path is readable (and saves can go next to the ROM) */
            String id = null;
            try {
                id = DocumentsContract.getDocumentId(u);
            } catch (IllegalArgumentException e) {
                try {
                    id = DocumentsContract.getTreeDocumentId(u);
                } catch (IllegalArgumentException e2) {
                    Log.w(TAG, "not a document: " + u);
                }
            }
            String path = id != null ? documentIdToPath(id) : null;
            if (path != null && new File(path).canRead()) return path;
            Log.w(TAG, "can't read " + path + " directly, copying it");
        }
        /* other apps' providers (a frontend's FileProvider, the media store) often
         * tell the real path; with All files access it can be read in place, so
         * the battery save stays next to the ROM */
        String real = realPath(c, u);
        if (real != null) return real;
        return copyIn(c, u);
    }

    /** "primary:ROMs/gba" -> /storage/emulated/0/ROMs/gba; other volumes are under /storage. */
    static String documentIdToPath(String id) {
        int colon = id.indexOf(':');
        if (colon < 0) return null;
        String volume = id.substring(0, colon), rest = id.substring(colon + 1);
        String root = "primary".equalsIgnoreCase(volume)
                ? Environment.getExternalStorageDirectory().getAbsolutePath()
                : "/storage/" + volume;
        return rest.isEmpty() ? root : root + "/" + rest;
    }

    private static String realPath(Context c, Uri u) {
        try (Cursor cur = c.getContentResolver().query(u, new String[] { "_data" }, null, null, null)) {
            if (cur != null && cur.moveToFirst() && !cur.isNull(0)) {
                String p = cur.getString(0);
                if (p != null && new File(p).canRead()) return p;
            }
        } catch (RuntimeException e) {
            /* no such column: fine */
        }
        /* FileProviders often keep the path in the URI: content://<authority>/<root>/storage/emulated/0/... */
        String up = u.getPath();
        if (up != null) {
            int i = up.indexOf("/storage/");
            if (i >= 0 && new File(up.substring(i)).canRead()) return up.substring(i);
        }
        return null;
    }

    /** Another app's file without a path: a copy in the app's folder (its saves stay there too). */
    private static String copyIn(Context c, Uri u) {
        String name = "game.gba";
        try (Cursor cur = c.getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cur != null && cur.moveToFirst() && cur.getString(0) != null) name = cur.getString(0).replace('/', '_');
        } catch (RuntimeException e) {
            Log.w(TAG, "no name for " + u + ": " + e);
        }
        File dir = c.getExternalFilesDir("launched");
        if (dir == null) dir = new File(c.getFilesDir(), "launched");
        dir.mkdirs();
        File out = new File(dir, name);
        try (InputStream is = c.getContentResolver().openInputStream(u); OutputStream os = new FileOutputStream(out)) {
            if (is == null) return null;
            byte[] buf = new byte[1 << 16];
            for (int n; (n = is.read(buf)) > 0; ) os.write(buf, 0, n);
        } catch (Exception e) {
            Log.e(TAG, "could not copy " + u + ": " + e);
            return null;
        }
        Log.i(TAG, "copied " + u + " to " + out);
        return out.getAbsolutePath();
    }
}
