package com.webkey;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class Webkey extends Activity {
    private final String TAG = "Webkey.Webkey";

	@Override
    public void onCreate(Bundle savedInstanceState) {
	    super.onCreate(savedInstanceState);
	    Log.d(TAG, "onCreate");

	    Toast.makeText(getApplicationContext(), "Starting Webkey service", Toast.LENGTH_LONG).show();

	    Intent svc = new Intent(this, Service.class);
	    svc.putExtra("token", "4f53b5fa68c0059b394f97bead76e115ab0a71d4");
	    startService(svc);
	    finish();
	}

}
