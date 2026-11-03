package io.github.pokeparkrecomp;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * The app's activity: NativeActivity (window_android.c runs the game), and
 * the one thing it can't do, asking for the player's disc image. The file
 * picker's answer comes back to an activity, so this asks, keeps a lasting
 * permission to read the file, remembers it, and hands the native side an
 * open file descriptor (disc_image_android.c reads the game from it).
 * And for the update check (update.c): fetching the latest release's
 * details, and opening its page in the browser.
 */
public class PokeParkActivity extends NativeActivity {
    private static final int PICK = 1;
    private static final String DISC = "disc";
    private volatile int pickedFd = -1;
    private String asking;

    private SharedPreferences prefs() {
        return getSharedPreferences("pokepark", MODE_PRIVATE);
    }

    private int open(Uri uri) {
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
            return pfd == null ? -1 : pfd.detachFd();
        } catch (Exception e) {
            Log.w("PokePark", "can't open " + uri, e);
            return -1;
        }
    }

    /** The disc image chosen before, opened, or -1 (from native code). */
    public int savedGameFd() {
        String uri = prefs().getString(DISC, null);
        return uri == null ? -1 : open(Uri.parse(uri));
    }

    /** It wasn't the game after all (from native code). */
    public void forgetGame() {
        prefs().edit().remove(DISC).apply();
    }

    /** The disc image just chosen, once, or -1 (polled from native code). */
    public int takePickedFd() {
        int fd = pickedFd;
        pickedFd = -1;
        return fd;
    }

    /** An HTTPS GET's body, or null (from native code, off the main thread). */
    public String httpGet(String url, String userAgent) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(10000);
            c.setReadTimeout(10000);
            c.setRequestProperty("Accept", "application/vnd.github+json");
            c.setRequestProperty("User-Agent", userAgent);
            if (c.getResponseCode() != 200) return null;
            InputStream in = c.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            for (int n; out.size() < 256 * 1024 && (n = in.read(buf)) > 0;) out.write(buf, 0, n);
            in.close();
            return out.toString("UTF-8");
        } catch (Exception e) {
            Log.w("PokePark", "update check: " + e);
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** Opens url in the browser (from native code). */
    public void openPage(final String url) {
        runOnUiThread(() -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception e) {
                Log.w("PokePark", "can't open " + url, e);
            }
        });
    }

    /** Asks for the disc image, saying message (from native code). */
    public void chooseGame(final String message) {
        runOnUiThread(new Runnable() {
            public void run() {
                ask(message);
            }
        });
    }

    private void ask(String message) {
        asking = message;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("PokePark")
            .setMessage(message)
            .setCancelable(false)
            .setPositiveButton("Choose disc image", (d, w) -> {
                Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.setType("*/*");
                startActivityForResult(pick, PICK);
            })
            .setNegativeButton("Quit", (d, w) -> finish())
            .show();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request != PICK) {
            super.onActivityResult(request, result, data);
            return;
        }
        Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            ask(asking); // backed out of the picker: there's nothing to play yet
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w("PokePark", "no lasting permission for " + uri, e);
        }
        int fd = open(uri);
        if (fd < 0) {
            ask("That file couldn't be opened. Choose your disc image of PokePark Wii again.");
            return;
        }
        prefs().edit().putString(DISC, uri.toString()).apply();
        pickedFd = fd;
    }
}
