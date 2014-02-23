package com.webkey;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Toast;

import com.webkey.activity.WebkeyTab.UpgradeThread;

import java.io.File;


public class Webkey extends Activity {
    private final String TAG = "Webkey.Webkey";

	@Override
    public void onCreate(Bundle savedInstanceState) {
	    super.onCreate(savedInstanceState);
	    Log.d(TAG, "onCreate");

	    Toast.makeText(getApplicationContext(), "Starting Webkey service", Toast.LENGTH_LONG).show();

	    Intent svc = new Intent(this, Service.class);
	    svc.putExtra("token", "deadc0de");
	    startService(svc);
	    finish();
	}

}
