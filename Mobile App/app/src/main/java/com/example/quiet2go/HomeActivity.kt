package com.example.quiet2go

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.*
import android.provider.Settings
import android.support.annotation.RequiresApi
import android.support.v7.app.AppCompatActivity
import android.widget.Button
import android.widget.TextView
import okhttp3.*
import org.json.JSONObject
import java.io.IOException

class HomeActivity : AppCompatActivity() {

    private lateinit var connectBtn: Button
    private lateinit var soundTxt: TextView
    private lateinit var soundLevelTxt: TextView

    private val client = OkHttpClient()
    private val handler = Handler(Looper.getMainLooper())
    private val pollingInterval = 1000L // 1 second

    @RequiresApi(Build.VERSION_CODES.M)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_home)

        connectBtn = findViewById(R.id.connectBtn)
        soundTxt = findViewById(R.id.soundTxt)
        soundLevelTxt = findViewById(R.id.soundLevelTxt)

        requestLocationPermission()

        connectBtn.setOnClickListener {
            val intent = Intent(Settings.ACTION_WIFI_SETTINGS)
            startActivity(intent)
        }
    }

    override fun onResume() {
        super.onResume()
        startPolling()
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private fun requestLocationPermission() {
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), 1)
        }
    }

    private fun isConnectedToSoundBot(): Boolean {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val info = wifiManager.connectionInfo
        return info != null && info.ssid == "\"SoundBot-AP\""
    }

    private fun startPolling() {
        handler.post(object : Runnable {
            override fun run() {
                if (isConnectedToSoundBot()) {
                    val request = Request.Builder()
                        .url("http://192.168.4.1/status")
                        .build()

                    client.newCall(request).enqueue(object : Callback {
                        override fun onFailure(call: Call, e: IOException) {
                            runOnUiThread {
                                soundTxt.text = "N/A"
                                soundLevelTxt.text = "N/A"
                            }
                        }

                        override fun onResponse(call: Call, response: Response) {
                            response.body?.string()?.let {
                                try {
                                    val json = JSONObject(it)
                                    val avg = json.getDouble("avg")
                                    val level = json.getInt("level")

                                    runOnUiThread {
                                        soundTxt.text = avg.toString()
                                        soundLevelTxt.text = level.toString()
                                    }
                                } catch (e: Exception) {
                                    e.printStackTrace()
                                }
                            }
                        }
                    })
                } else {
                    runOnUiThread {
                        soundTxt.text = "Not connected"
                        soundLevelTxt.text = "-"
                    }
                }

                handler.postDelayed(this, pollingInterval)
            }
        })
    }
}
