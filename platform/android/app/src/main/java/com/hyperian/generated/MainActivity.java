package com.hyperian.generated;

import android.app.Activity;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

public final class MainActivity extends Activity {
    static { System.loadLibrary("hyperian_mobile_jni"); }
    private native String openMobile(String bytecode, String data);
    private native void setMobileValue(String name, String value);
    private native String runMobileAction(String action);
    private native String sendMobileEvent(String event);
    private native String renderMobile();
    private native void closeMobile();

    private final Map<String, View> inputs = new LinkedHashMap<>();
    private LinearLayout content;
    private final Handler timers = new Handler(Looper.getMainLooper());
    private boolean timersStarted;
    private boolean mobileReady;
    private GestureDetector gestures;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this); content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL); int space = pixels(16); content.setPadding(space, space, space, space);
        scroll.addView(content); setContentView(scroll);
        gestures = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent event) { return true; }
            @Override public boolean onSingleTapConfirmed(MotionEvent event) { sendGestureEvent("TAP"); return true; }
            @Override public void onLongPress(MotionEvent event) { sendGestureEvent("LONG_PRESS"); }
            @Override public boolean onFling(MotionEvent first, MotionEvent last, float velocityX, float velocityY) {
                if (first == null || last == null) return false;
                float horizontal = last.getX() - first.getX(), vertical = last.getY() - first.getY();
                if (Math.max(Math.abs(horizontal), Math.abs(vertical)) < pixels(50) ||
                    Math.max(Math.abs(velocityX), Math.abs(velocityY)) < pixels(100)) return false;
                String direction = Math.abs(horizontal) >= Math.abs(vertical) ? (horizontal < 0 ? "left" : "right") : (vertical < 0 ? "up" : "down");
                sendGestureEvent("SWIPE:" + direction); return true;
            }
        });
        scroll.setOnTouchListener((view, event) -> { gestures.onTouchEvent(event); return false; });
        try {
            File bytecode = copyAsset("application.hyc");
            String error = openMobile(bytecode.getAbsolutePath(), new File(getFilesDir(), "hyperian-data.db").getAbsolutePath());
            if (!error.isEmpty()) throw new Exception(error);
            mobileReady = true; render();
        } catch (Exception error) { showError(error.getMessage()); }
    }

    private File copyAsset(String name) throws Exception {
        File output = new File(getFilesDir(), name);
        try (InputStream input = getAssets().open(name); FileOutputStream file = new FileOutputStream(output)) {
            byte[] buffer = new byte[16384]; int count;
            while ((count = input.read(buffer)) != -1) file.write(buffer, 0, count);
        }
        return output;
    }

    private static String[] fetchFromInternet(String address, int limit) {
        final String[][] completed = {null};
        Thread worker = new Thread(() -> completed[0] = fetchOnWorker(address, limit), "Hyperian HTTPS");
        worker.start();
        try { worker.join(); }
        catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); return new String[] {"0", "", "the HTTPS request was interrupted"}; }
        return completed[0] == null ? new String[] {"0", "", "Android could not finish its HTTPS request"} : completed[0];
    }

    private static String[] fetchOnWorker(String address, int limit) {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(address);
            if (!url.getProtocol().equals("https") && !url.getProtocol().equals("http"))
                return new String[] {"0", "", "phone requests require an http or https address"};
            connection = (HttpURLConnection)url.openConnection();
            connection.setConnectTimeout(10000); connection.setReadTimeout(30000); connection.setInstanceFollowRedirects(true);
            connection.setRequestProperty("User-Agent", "Hyperian Mobile");
            int status = connection.getResponseCode();
            InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            if (stream != null) try (InputStream input = stream) {
                byte[] buffer = new byte[4096]; int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count >= limit) return new String[] {"0", "", "the web response is larger than " + (limit - 1) + " characters"};
                    bytes.write(buffer, 0, count);
                }
            }
            return new String[] {Integer.toString(status), new String(bytes.toByteArray(), StandardCharsets.UTF_8), ""};
        } catch (Exception problem) {
            return new String[] {"0", "", "web request failed: " + problem.getMessage()};
        } finally { if (connection != null) connection.disconnect(); }
    }

    private void render() {
        try {
            JSONObject screen = new JSONObject(renderMobile());
            if (screen.has("error")) throw new Exception(screen.getString("error"));
            content.removeAllViews(); inputs.clear(); addControls(screen.getJSONArray("controls"), content);
            if (!timersStarted) { timersStarted = true; JSONArray intervals = screen.getJSONArray("timers");
                for (int index = 0; index < intervals.length(); index++) scheduleTimer(intervals.getLong(index)); }
        } catch (Exception error) { showError(error.getMessage()); }
    }

    private void addControls(JSONArray controls, LinearLayout parent) throws Exception {
        for (int index = 0; index < controls.length(); index++) addControl(controls.getJSONObject(index), parent);
    }

    private void addControl(JSONObject control, LinearLayout parent) throws Exception {
        String kind = control.getString("kind"); View view;
        if (kind.equals("row") || kind.equals("column") || kind.equals("card")) {
            LinearLayout group = new LinearLayout(this);
            group.setOrientation(kind.equals("row") ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
            int inset = pixels(kind.equals("card") ? 14 : 4); group.setPadding(inset, inset, inset, inset);
            if (kind.equals("card")) {
                GradientDrawable card = new GradientDrawable(); card.setColor(Color.argb(18, 127, 127, 127));
                card.setStroke(pixels(1), Color.argb(80, 127, 127, 127)); card.setCornerRadius(pixels(12));
                group.setBackground(card); group.setElevation(pixels(2));
            }
            addControls(control.getJSONArray("children"), group); view = group;
        } else if (kind.equals("heading") || kind.equals("text") || kind.equals("value")) {
            TextView text = new TextView(this); text.setText(control.optString("text"));
            text.setTextSize(kind.equals("heading") ? 28 : 17); if (kind.equals("heading")) text.setTypeface(null, Typeface.BOLD); view = text;
        } else if (kind.equals("input") || kind.equals("textarea")) {
            EditText input = new EditText(this); input.setHint(control.optString("label")); input.setText(control.optString("value"));
            if (kind.equals("textarea")) { input.setMinLines(4); input.setGravity(android.view.Gravity.TOP); input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE); }
            else input.setSingleLine(true);
            String inputName = control.getString("name"), changeEvent = control.optString("changeEvent"), submitEvent = control.optString("submitEvent");
            inputs.put(inputName, input);
            if (!changeEvent.isEmpty()) input.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence text, int start, int count, int after) {}
                @Override public void onTextChanged(CharSequence text, int start, int before, int count) {}
                @Override public void afterTextChanged(Editable text) { sendInputEvent(changeEvent); }
            });
            if (!submitEvent.isEmpty()) input.setOnEditorActionListener((ignored, action, event) -> { sendInputEvent(submitEvent); return true; });
            view = input;
        } else if (kind.equals("checkbox")) {
            CheckBox input = new CheckBox(this); input.setText(control.optString("label")); input.setChecked(control.optString("value").equals("true"));
            String inputName = control.getString("name"), changeEvent = control.optString("changeEvent"); inputs.put(inputName, input);
            if (!changeEvent.isEmpty()) input.setOnCheckedChangeListener((ignored, checked) -> sendInputEvent(changeEvent)); view = input;
        } else if (kind.equals("button")) {
            Button button = new Button(this); button.setText(control.optString("label")); String action = control.optString("action");
            button.setEnabled(!action.isEmpty()); button.setOnClickListener(ignored -> runAction(action)); view = button;
        } else if (kind.equals("link")) {
            Button link = new Button(this); link.setText(control.optString("label")); String destination = control.optString("destination");
            link.setOnClickListener(ignored -> startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(destination)))); view = link;
        } else if (kind.equals("image")) {
            ImageView image = new ImageView(this); image.setContentDescription(control.optString("description"));
            try (InputStream input = getAssets().open(control.getString("source"))) { image.setImageBitmap(BitmapFactory.decodeStream(input)); }
            view = image;
        } else {
            TextView description = new TextView(this); description.setText(control.optString("description", control.optString("source"))); view = description;
        }
        int width = parent.getOrientation() == LinearLayout.HORIZONTAL ? 0 : LinearLayout.LayoutParams.MATCH_PARENT;
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(width, LinearLayout.LayoutParams.WRAP_CONTENT,
            parent.getOrientation() == LinearLayout.HORIZONTAL ? 1 : 0);
        layout.setMargins(0, 0, pixels(10), pixels(10)); parent.addView(view, layout);
    }

    private void runAction(String action) {
        syncInputs();
        String error = runMobileAction(action); if (!error.isEmpty()) Toast.makeText(this, error, Toast.LENGTH_LONG).show(); render();
    }

    private void syncInputs() {
        for (Map.Entry<String, View> item : inputs.entrySet()) {
            String value = item.getValue() instanceof CheckBox ? (((CheckBox)item.getValue()).isChecked() ? "true" : "false") : ((EditText)item.getValue()).getText().toString();
            setMobileValue(item.getKey(), value);
        }
    }

    private void sendInputEvent(String event) {
        content.post(() -> {
            syncInputs(); String error = sendMobileEvent(event);
            if (!error.isEmpty()) Toast.makeText(MainActivity.this, error, Toast.LENGTH_LONG).show(); render();
        });
    }

    private void sendLifecycleEvent(String event, boolean renderAfterward) {
        syncInputs(); String error = sendMobileEvent(event);
        if (!error.isEmpty()) Toast.makeText(this, error, Toast.LENGTH_LONG).show();
        if (renderAfterward) render();
    }

    private void sendGestureEvent(String event) {
        content.post(() -> sendLifecycleEvent(event, true));
    }

    private void scheduleTimer(long interval) {
        timers.postDelayed(new Runnable() { @Override public void run() {
            String error = sendMobileEvent("TIMER:" + interval);
            if (!error.isEmpty()) Toast.makeText(MainActivity.this, error, Toast.LENGTH_LONG).show();
            render(); timers.postDelayed(this, interval);
        }}, interval);
    }

    private int pixels(int amount) { return (int)(amount * getResources().getDisplayMetrics().density + 0.5f); }
    private void showError(String message) { content.removeAllViews(); TextView error = new TextView(this); error.setText("Hyperian could not start: " + message); content.addView(error); }
    @Override protected void onResume() { super.onResume(); if (mobileReady) sendLifecycleEvent("RESUME", true); }
    @Override protected void onPause() { if (mobileReady) sendLifecycleEvent("PAUSE", false); super.onPause(); }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused); if (mobileReady) sendLifecycleEvent(focused ? "FOCUS" : "BLUR", focused);
    }
    @Override protected void onDestroy() { mobileReady = false; timers.removeCallbacksAndMessages(null); closeMobile(); super.onDestroy(); }
}
