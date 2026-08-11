package com.hyperian.generated;

import android.app.Activity;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
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
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
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

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this); content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL); int space = pixels(16); content.setPadding(space, space, space, space);
        scroll.addView(content); setContentView(scroll);
        try {
            File bytecode = copyAsset("application.hyc");
            String error = openMobile(bytecode.getAbsolutePath(), new File(getFilesDir(), "hyperian-data.hdb").getAbsolutePath());
            if (!error.isEmpty()) throw new Exception(error);
            render();
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

    private void render() {
        try {
            JSONObject screen = new JSONObject(renderMobile());
            if (screen.has("error")) throw new Exception(screen.getString("error"));
            content.removeAllViews(); inputs.clear(); JSONArray controls = screen.getJSONArray("controls");
            for (int index = 0; index < controls.length(); index++) addControl(controls.getJSONObject(index));
            if (!timersStarted) { timersStarted = true; JSONArray intervals = screen.getJSONArray("timers");
                for (int index = 0; index < intervals.length(); index++) scheduleTimer(intervals.getLong(index)); }
        } catch (Exception error) { showError(error.getMessage()); }
    }

    private void addControl(JSONObject control) throws Exception {
        String kind = control.getString("kind"); View view;
        if (kind.equals("heading") || kind.equals("text") || kind.equals("value")) {
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
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        layout.setMargins(0, 0, 0, pixels(10)); content.addView(view, layout);
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

    private void scheduleTimer(long interval) {
        timers.postDelayed(new Runnable() { @Override public void run() {
            String error = sendMobileEvent("TIMER:" + interval);
            if (!error.isEmpty()) Toast.makeText(MainActivity.this, error, Toast.LENGTH_LONG).show();
            render(); timers.postDelayed(this, interval);
        }}, interval);
    }

    private int pixels(int amount) { return (int)(amount * getResources().getDisplayMetrics().density + 0.5f); }
    private void showError(String message) { content.removeAllViews(); TextView error = new TextView(this); error.setText("Hyperian could not start: " + message); content.addView(error); }
    @Override protected void onDestroy() { timers.removeCallbacksAndMessages(null); closeMobile(); super.onDestroy(); }
}
