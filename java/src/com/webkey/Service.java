package com.webkey;

import android.annotation.SuppressLint;
import android.app.IntentService;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

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

    @SuppressLint("SdCardPath")
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
                    "su", "-c", "killall", "webkey"
                };
            File file = new File("/");
            if ( webkeyserver != null ) {
                Log.d(TAG, "destroying existing server");
                webkeyserver.destroy();
                webkeyserver = null;
            }
            Log.d(TAG, "killing webkey");
            try {
                webkeyserver = Runtime.getRuntime().exec(cmd, null, file);
                Thread.sleep(2000);
            } catch (IOException e) {
                e.printStackTrace();
                return;
            } catch (InterruptedException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }
            Toast.makeText(getApplicationContext(), R.string.main_upgradingwait, Toast.LENGTH_LONG).show();
            if (!unpackAssets()) {
                Log.e(TAG, "unpacking of assets failed");
                Toast.makeText(getApplicationContext(), "Webkey binary install failed.", Toast.LENGTH_LONG).show();
                try {
                    Thread.sleep(2000);
                } catch (InterruptedException e) {
                    // TODO Auto-generated catch block
                    e.printStackTrace();
                }
                return;
            }

//            if(!binIO.rootCheck()){
//                Log.e(TAG, "Root check failed.");
//                Toast.makeText(getApplicationContext(), "Webkey root check failed.", Toast.LENGTH_LONG).show();
//                try {
//                    Thread.sleep(2000);
//                } catch (InterruptedException e) {
//                    // TODO Auto-generated catch block
//                    e.printStackTrace();
//                }
//                return;
//            }
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
                "/data/data/com.webkey/files/bin/start.sh",
                token,
                "60",
                "on4today.com",
                "3001"
            };
            File file = new File("/");
            if ( webkeyserver != null ) {
                Log.d(TAG, "destroying existing server");
                webkeyserver.destroy();
                webkeyserver = null;
            }
            Log.d(TAG, "starting server");
            webkeyserver = Runtime.getRuntime().exec(cmd, null, file);

            InputStream error = webkeyserver.getErrorStream();
            InputStream input = webkeyserver.getInputStream();
            BufferedReader errorBuffer = new BufferedReader(new InputStreamReader(error));
            String line = "";
            StringBuilder stringBuilder = new StringBuilder();
            while ((line = errorBuffer.readLine()) != null)
            {
                stringBuilder.append(line).append("\n");
            }
            Log.e(TAG, "STDERR " + stringBuilder.toString());

            BufferedReader inputBuffer = new BufferedReader(new InputStreamReader(input));
            stringBuilder = new StringBuilder();
            while ((line = inputBuffer.readLine()) != null)
            {
                stringBuilder.append(line).append("\n");
            }
            Log.e(TAG, "STDERR " + stringBuilder.toString());

            Log.d(TAG, "started server");
        } catch (IOException e) {
            e.printStackTrace();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    boolean unpackAssets() {
        if (unpackAssetFolder("bin")) {
            if (unpackAssetFolder("certs")) {
                chmod(getFilesDir().getPath() + "/bin/webkey");
                chmod(getFilesDir().getPath() + "/bin/openssl");
                chmod(getFilesDir().getPath() + "/bin/ssleay.cnf");
                chmod(getFilesDir().getPath() + "/bin/start.sh");
                return true;
            }
        }
        return false;
    }

    boolean unpackAssetFolder(String folder) {
        AssetManager assetManager = getAssets();
        String[] files = null;
        try {
            files = assetManager.list(folder);
        } catch (IOException e) {
            Log.e("tag", "Failed to get asset file list.", e);
            return false;
        }
        for(String filename : files) {
            Log.d(TAG, "Unpacking file " + folder + "/" + filename );
            InputStream in = null;
            FileOutputStream out = null;
            try {
              in = assetManager.open(folder + "/" + filename);
              new File(getFilesDir().getPath()+ "/" + folder).mkdir();
              File outFile = new File(getFilesDir().getPath() + "/" + folder, filename);
              out = new FileOutputStream(outFile);

              byte[] buffer = new byte[1024];
              int read;
              while((read = in.read(buffer)) != -1){
                out.write(buffer, 0, read);
              }

            } catch(IOException e) {
                Log.e("tag", "Failed to copy asset file: " + filename, e);
                return false;
            } finally {
                try {
                    if (in != null) in.close();
                    if (out != null) {
                        out.flush();
                        out.close();
                    }
                } catch (IOException e) {
                }
                in = null;
                out = null;
            }
        }
        return true;
    }

    void chmod(String file) {

        Log.d(TAG,"trying /system/bin/chmod");
        try {
            String[] cmd = { "/system/bin/chmod", "775",
                    file };
            File location = new File("/system/bin/");
            Process p = Runtime.getRuntime().exec(cmd, null, location);
            p.waitFor();
            Log.d(TAG,"success");

        } catch (IOException ioe) {
            Log.d(TAG,"failed");
        } catch (InterruptedException e) {
            Log.d(TAG,"failed");
        }

    }

}
