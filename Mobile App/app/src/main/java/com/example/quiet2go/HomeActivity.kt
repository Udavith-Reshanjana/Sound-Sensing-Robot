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
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
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
        val connectedSSID = info.ssid
        Log.d("Wi-Fi Info", "SSID: $connectedSSID") // Logs the connected SSID
        Toast.makeText(this, "Wi-Fi SSID: $connectedSSID", Toast.LENGTH_SHORT).show()
        return info != null && connectedSSID == "\"SoundBot-AP\""
    }

    private fun startPolling() {
        handler.post(object : Runnable {
            override fun run() {
                if (isConnectedToSoundBot()) {
                    val request = Request.Builder()
                        .url("http://192.168.4.1/status")
                        .build()

                    Log.d("HTTP Request", "Sending request to: http://192.168.4.1/status") // Log the request URL
                    Toast.makeText(this@HomeActivity, "Sending API Request...", Toast.LENGTH_SHORT).show()

                    client.newCall(request).enqueue(object : Callback {
                        override fun onFailure(call: Call, e: IOException) {
                            Log.e("HTTP Error", "Request failed: ${e.message}")
                            runOnUiThread {
                                Toast.makeText(this@HomeActivity, "Request Failed: ${e.message}", Toast.LENGTH_LONG).show()
                                soundTxt.text = "N/A"
                                soundLevelTxt.text = "N/A"
                            }
                        }

                        override fun onResponse(call: Call, response: Response) {
                            val responseBody = response.body
                            if (responseBody != null) {
                                val responseString = responseBody.string()
                                Log.d("HTTP Response", responseString) // Logs the raw HTTP response
                                runOnUiThread {
                                    Toast.makeText(this@HomeActivity, "Received Response: $responseString", Toast.LENGTH_SHORT).show()
                                }
                                try {
                                    val json = JSONObject(responseString)
                                    val avg = json.getDouble("avg")
                                    val level = json.getInt("level")

                                    Log.d("Parsed Data", "Avg: $avg, Level: $level") // Log parsed values

                                    runOnUiThread {
                                        soundTxt.text = avg.toString()
                                        soundLevelTxt.text = level.toString()
                                        Toast.makeText(this@HomeActivity, "Data Updated: Avg = $avg, Level = $level", Toast.LENGTH_SHORT).show()
                                    }
                                } catch (e: Exception) {
                                    Log.e("JSON Parse Error", "Error parsing JSON: ${e.message}")
                                    runOnUiThread {
                                        Toast.makeText(this@HomeActivity, "Parse Error: ${e.message}", Toast.LENGTH_LONG).show()
                                        soundTxt.text = "Parse Error"
                                        soundLevelTxt.text = "Parse Error"
                                    }
                                }
                            } else {
                                Log.e("HTTP Error", "Empty response from server.")
                                runOnUiThread {
                                    Toast.makeText(this@HomeActivity, "Empty Response", Toast.LENGTH_SHORT).show()
                                    soundTxt.text = "Empty Response"
                                    soundLevelTxt.text = "Empty Response"
                                }
                            }
                        }
                    })
                } else {
                    runOnUiThread {
                        Log.w("Wi-Fi Connection", "Not connected to SoundBot-AP")
                        Toast.makeText(this@HomeActivity, "Not connected to SoundBot-AP", Toast.LENGTH_SHORT).show()
                        soundTxt.text = "Not connected"
                        soundLevelTxt.text = "-"
                    }
                }

                handler.postDelayed(this, pollingInterval)
            }
        })
    }
}
