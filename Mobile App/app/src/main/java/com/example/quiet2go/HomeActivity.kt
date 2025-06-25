package com.example.quiet2go

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.media.MediaPlayer
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
    private var hasShownConnectedToast = false // Tracks if the Toast has already been shown
    private var isCurrentlyConnected = false // Tracks the current connection status

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
            VibrationUtil.triggerVibration(this)
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
        val isConnected = info != null && connectedSSID == "\"SoundBot-AP\""

        // Show Toast once if connected and update button state
        if (isConnected && !hasShownConnectedToast) {
            hasShownConnectedToast = true
            isCurrentlyConnected = true
            runOnUiThread {
                Toast.makeText(this, "Successfully connected to $connectedSSID", Toast.LENGTH_SHORT).show()
                connectBtn.text = "Connected"
                connectBtn.setBackgroundColor(Color.RED)
            }
        } else if (!isConnected && isCurrentlyConnected) {
            isCurrentlyConnected = false
            runOnUiThread {
                connectBtn.text = "Connect"
                connectBtn.setBackgroundColor(resources.getColor(R.color.purple_200))
            }
        }

        return isConnected
    }

    private fun startPolling() {
        handler.post(object : Runnable {
            override fun run() {
                if (isConnectedToSoundBot()) {
                    val request = Request.Builder()
                        .url("http://192.168.4.1/status")
                        .build()

                    Log.d("HTTP Request", "Sending request to: http://192.168.4.1/status") // Log the request URL

                    client.newCall(request).enqueue(object : Callback {
                        override fun onFailure(call: Call, e: IOException) {
                            Log.e("HTTP Error", "Request failed: ${e.message}")
                            runOnUiThread {
                                soundTxt.text = "N/A"
                                soundLevelTxt.text = "N/A"
                            }
                        }

                        @SuppressLint("SetTextI18n")
                        override fun onResponse(call: Call, response: Response) {
                            val responseBody = response.body
                            if (responseBody != null) {
                                val responseString = responseBody.string()
                                Log.d("HTTP Response", responseString) // Logs the raw HTTP response
                                try {
                                    val json = JSONObject(responseString)
                                    val avg = json.getDouble("avg")
                                    val level = json.getInt("level")

                                    val soundLevel = when (level) {
                                        1 -> "Very Quiet"
                                        2 -> "Moderate"
                                        3 -> "Loud"
                                        else -> "Very Loud"
                                    }

                                    val textcolor = when (level) {
                                        1 -> Color.GREEN
                                        2 -> Color.YELLOW
                                        3 -> Color.rgb(255, 165, 0)
                                        else -> Color.RED
                                    }

                                    Log.d("Parsed Data", "Avg: $avg, Level: $level") // Log parsed values

                                    runOnUiThread {
                                        soundTxt.text = "${if (avg >= 0) avg * 0.01 else 0} dB"
                                        soundLevelTxt.text = soundLevel
                                        soundLevelTxt.setTextColor(textcolor)
                                    }
                                } catch (e: Exception) {
                                    Log.e("JSON Parse Error", "Error parsing JSON: ${e.message}")
                                    runOnUiThread {
                                        soundTxt.text = "Parse Error"
                                        soundLevelTxt.text = "Parse Error"
                                        soundLevelTxt.setTextColor(Color.rgb(0, 0, 139))
                                    }
                                }
                            } else {
                                Log.e("HTTP Error", "Empty response from server.")
                                runOnUiThread {
                                    soundTxt.text = "Empty Response"
                                    soundLevelTxt.text = "Empty Response"
                                    soundLevelTxt.setTextColor(Color.rgb(0, 0, 139))
                                }
                            }
                        }
                    })
                } else {
                    runOnUiThread {
                        Log.w("Wi-Fi Connection", "Not connected to SoundBot-AP")
                        soundTxt.text = "Not connected"
                        soundLevelTxt.text = "-"
                        soundLevelTxt.setTextColor(Color.rgb(0, 0, 139))
                    }
                }

                handler.postDelayed(this, pollingInterval)
            }
        })
    }
}

object VibrationUtil {
    fun triggerVibration(context: Context, duration: Long = 150) {
        playClickSound(context)
        val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator?

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val vibrationEffect = VibrationEffect.createOneShot(
                duration, // Duration in milliseconds
                VibrationEffect.DEFAULT_AMPLITUDE // Default amplitude
            )
            vibrator?.cancel()
            vibrator?.vibrate(vibrationEffect)
        } else {
            vibrator?.vibrate(duration) // Deprecated in API 26
        }
    }

    fun triggerVibrationshort(context: Context) {
        playClickSound(context)
        val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator?

        val vibrationEffect3: VibrationEffect

        // this type of vibration requires API 29
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {

            // create vibrator effect with the constant EFFECT_DOUBLE_CLICK
            vibrationEffect3 =
                VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK)

            // it is safe to cancel other vibrations currently taking place
            if (vibrator != null) {
                vibrator.cancel()
            }
            if (vibrator != null) {
                vibrator.vibrate(vibrationEffect3)
            }
        }
    }

    private fun playClickSound(context: Context) {
        val mediaPlayer = MediaPlayer.create(context, R.raw.click) // Replace with your sound resource
        mediaPlayer.setOnCompletionListener { mp ->
            mp.release() // Release resources after playback is complete
        }
        mediaPlayer.start()
    }
}
