package com.webkey;

import android.app.IntentService;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;

public class Service extends IntentService {
    private final String TAG = "Webkey.Service";
    private static Process webkeyserver = null;

    public Service() {
        super("WebkeyService");
        Log.d(TAG, "Ctor");
    }

    @Override
    public void onCreate() {
        Log.d(TAG, "onCreate");
        super.onCreate();
    }

    @Override
    public void onDestroy() {
        Log.d(TAG, "onDestroy");
        super.onDestroy();
    }

    @Override
    protected void onHandleIntent(Intent intent) {
        Log.d(TAG, "onHandleIntent");

        SharedPreferences wmbPreference = PreferenceManager.getDefaultSharedPreferences(this);
        boolean isFirstRun = wmbPreference.getBoolean("freshinstall", true);
        if (isFirstRun)
        {
            Log.w(TAG, "Fresh install or upgrade, unpacking binaries.");
            BinIO binIO = new BinIO(getApplicationContext());
            Toast.makeText(getApplicationContext(), R.string.main_upgradingwait, Toast.LENGTH_LONG).show();
            String path = getFilesDir().getPath();
            boolean webkeyupgrade = false;
            webkeyupgrade = binIO.unpackSingleFile("bin", "webkey", path);
            binIO.delDir(new File(path+"/plugins"));
            binIO.delDir(new File(path+"/client"));
            if(!(webkeyupgrade && binIO.openAssets("webkey", "", path) &&
                    binIO.chmod775("webkey") &&
                    binIO.chmod775("openssl") &&
                    binIO.chmod775("start.sh")))
            {
                Log.e(TAG, "Binary unpacking failed.");
                Toast.makeText(getApplicationContext(), "Webkey binary install failed.", Toast.LENGTH_LONG).show();
                return;
            }
            if(!binIO.rootCheck()){
                Log.e(TAG, "Root check failed.");
                Toast.makeText(getApplicationContext(), "Webkey root check failed.", Toast.LENGTH_LONG).show();
                return;
            }
            SharedPreferences.Editor editor = wmbPreference.edit();
            editor.putBoolean("freshinstall", false);
            editor.commit();
        }

        try {
            Log.d(TAG, "start server");
            String token = intent.getStringExtra("token");
            if (token == null) {
                Log.e(TAG, "No token received on intent");
                return;
            }
            String[] cmd = {
                "/data/data/com.webkey/files/start.sh",
                token
            };
            File file = new File("/");
            if ( webkeyserver != null ) {
                Log.d(TAG, "destroying existing server");
                webkeyserver.destroy();
                webkeyserver = null;
            }
            Log.d(TAG, "starting server");
            webkeyserver = Runtime.getRuntime().exec(cmd, null, file);
            Log.d(TAG, "started server");
        } catch (IOException e) {
            e.printStackTrace();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }


}
