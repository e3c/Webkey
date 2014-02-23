package com.webkey;

import android.app.IntentService;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
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

        String curVersion = "unknown";
        try {
            ComponentName comp = new ComponentName(getApplicationContext(), getApplicationContext().getClass());
            PackageInfo pinfo = getApplicationContext().getPackageManager().getPackageInfo(comp.getPackageName(), 0);


            curVersion = pinfo.versionName;

        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            //do nothing
        }
        SharedPreferences wmbPreference = PreferenceManager.getDefaultSharedPreferences(this);
        String installedVersion = wmbPreference.getString("installedVersion", "none");
        if (!installedVersion.equals(curVersion))
        {
            Log.w(TAG, "Current version is " +  curVersion + " and last installedVersion was " + installedVersion);
            String[] cmd = {
                    "su -c killall webkey"
                };
            File file = new File("/");
            if ( webkeyserver != null ) {
                Log.d(TAG, "destroying existing server");
                webkeyserver.destroy();
                webkeyserver = null;
            }
            Log.d(TAG, "killing webkeyr");
            try {
                webkeyserver = Runtime.getRuntime().exec(cmd, null, file);
            } catch (IOException e) {
                e.printStackTrace();
            }

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
            editor.putString("installedVersion", curVersion);
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
