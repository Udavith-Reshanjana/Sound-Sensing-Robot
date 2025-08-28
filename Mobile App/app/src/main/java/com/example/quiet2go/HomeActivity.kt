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
import java.text.DecimalFormat

class HomeActivity : AppCompatActivity() {

    private lateinit var connectBtn: Button
    private lateinit var soundTxt: TextView
    private lateinit var soundLevelTxt: TextView
    private lateinit var robotStateTxt: TextView
    private lateinit var distanceTxt: TextView
    private lateinit var mqttStatusTxt: TextView
    private lateinit var uptimeTxt: TextView

    private val client = OkHttpClient()
    private val handler = Handler(Looper.getMainLooper())
    private val pollingInterval = 1000L // 1 second
    private var isCurrentlyConnected = false // Tracks the current connection status
    private var lastSoundLevel = 1 // Track sound level changes
    private var consecutiveErrors = 0
    private val maxConsecutiveErrors = 5

    @RequiresApi(Build.VERSION_CODES.M)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_home)

        // Initialize views
        connectBtn = findViewById(R.id.connectBtn)
        soundTxt = findViewById(R.id.soundTxt)
        soundLevelTxt = findViewById(R.id.soundLevelTxt)
        robotStateTxt = findViewById(R.id.robotStateTxt)
        distanceTxt = findViewById(R.id.distanceTxt)
        mqttStatusTxt = findViewById(R.id.mqttStatusTxt)
        uptimeTxt = findViewById(R.id.uptimeTxt)

        requestLocationPermission()

        connectBtn.setOnClickListener {
            val intent = Intent(Settings.ACTION_WIFI_SETTINGS)
            VibrationUtil.triggerVibration(this)
            startActivity(intent)
        }

        // Initialize display
        resetDisplay()
    }

    override fun onResume() {
        super.onResume()
        startPolling()
    }

    override fun onPause() {
        super.onPause()
        stopPolling()
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
        Log.d("Wi-Fi Info", "SSID: $connectedSSID")
        val isConnected = info != null && connectedSSID == "\"SoundBot-AP\""

        if (isConnected && !isCurrentlyConnected) {
            isCurrentlyConnected = true
            consecutiveErrors = 0 // Reset error counter on successful connection
            runOnUiThread {
                Toast.makeText(this, "Successfully connected to SoundBot-AP", Toast.LENGTH_SHORT).show()
                connectBtn.text = "Connected"
                connectBtn.setBackgroundColor(Color.GREEN)
            }
        } else if (!isConnected && isCurrentlyConnected) {
            isCurrentlyConnected = false
            runOnUiThread {
                Toast.makeText(this, "Disconnected from SoundBot", Toast.LENGTH_SHORT).show()
                connectBtn.text = "Connect"
                connectBtn.setBackgroundColor(resources.getColor(R.color.purple_200))
                resetDisplay()
            }
        }

        return isConnected
    }

    private fun resetDisplay() {
        runOnUiThread {
            soundTxt.text = "Not connected"
            soundLevelTxt.text = "-"
            robotStateTxt.text = "Unknown"
            distanceTxt.text = "N/A"
            mqttStatusTxt.text = "N/A"
            uptimeTxt.text = "N/A"

            soundLevelTxt.setTextColor(Color.GRAY)
            robotStateTxt.setTextColor(Color.GRAY)
            distanceTxt.setTextColor(Color.GRAY)
            mqttStatusTxt.setTextColor(Color.GRAY)
        }
    }

    private fun getRobotStateText(state: Int): String {
        return when (state) {
            0 -> "Moving Forward"
            1 -> "Turning Left"
            2 -> "Backing Up"
            3 -> "Turning Right"
            4 -> "Stopped"
            else -> "Unknown State"
        }
    }

    private fun getRobotStateColor(state: Int): Int {
        return when (state) {
            0 -> Color.GREEN        // Moving Forward
            1 -> Color.BLUE         // Turning Left
            2 -> Color.rgb(255, 165, 0)  // Backing Up (Orange)
            3 -> Color.BLUE         // Turning Right
            4 -> Color.RED          // Stopped
            else -> Color.GRAY      // Unknown
        }
    }

    private fun formatUptime(uptimeMs: Long): String {
        val seconds = uptimeMs / 1000
        val minutes = seconds / 60
        val hours = minutes / 60
        val days = hours / 24

        return when {
            days > 0 -> "${days}d ${hours % 24}h ${minutes % 60}m"
            hours > 0 -> "${hours}h ${minutes % 60}m ${seconds % 60}s"
            minutes > 0 -> "${minutes}m ${seconds % 60}s"
            else -> "${seconds}s"
        }
    }

    private var pollingRunnable: Runnable? = null

    private fun startPolling() {
        stopPolling() // Stop any existing polling

        pollingRunnable = object : Runnable {
            override fun run() {
                if (isConnectedToSoundBot()) {
                    val request = Request.Builder()
                        .url("http://192.168.4.1/status")
                        .build()

                    Log.d("HTTP Request", "Sending request to: http://192.168.4.1/status")

                    client.newCall(request).enqueue(object : Callback {
                        override fun onFailure(call: Call, e: IOException) {
                            consecutiveErrors++
                            Log.e("HTTP Error", "Request failed: ${e.message} (Error count: $consecutiveErrors)")

                            runOnUiThread {
                                if (consecutiveErrors >= maxConsecutiveErrors) {
                                    Toast.makeText(this@HomeActivity, "Connection issues detected", Toast.LENGTH_SHORT).show()
                                    resetDisplay()
                                } else {
                                    soundTxt.text = "Connection error"
                                    soundLevelTxt.text = "Retrying..."
                                    soundLevelTxt.setTextColor(Color.RED)
                                }
                            }
                        }

                        @SuppressLint("SetTextI18n")
                        override fun onResponse(call: Call, response: Response) {
                            val responseBody = response.body
                            if (responseBody != null) {
                                val responseString = responseBody.string()
                                Log.d("HTTP Response", responseString)

                                try {
                                    val json = JSONObject(responseString)
                                    val avg = json.getDouble("avg")
                                    val level = json.getInt("level")
                                    val state = json.getInt("state")
                                    val distance = json.getInt("distance")
                                    val ambient = json.getDouble("ambient")
                                    val uptime = json.getLong("uptime")
                                    val mqttConnected = json.optBoolean("mqtt_connected", false)
                                    val connectedStations = json.optInt("connected_stations", 0)

                                    consecutiveErrors = 0 // Reset error counter on successful response

                                    // Sound level processing
                                    val soundLevel = when (level) {
                                        1 -> "Very Quiet"
                                        2 -> "Moderate"
                                        3 -> "Loud"
                                        else -> "Very Loud"
                                    }

                                    val textColor = when (level) {
                                        1 -> Color.GREEN
                                        2 -> Color.YELLOW
                                        3 -> Color.rgb(255, 165, 0)
                                        else -> Color.RED
                                    }

                                    // Sound calculation (matching Arduino logic)
                                    val adjustedAvg = if (avg >= 0) avg * 0.01 else 0.0
                                    val roundedAvg = if (adjustedAvg != 0.0) {
                                        val correctedAvg = adjustedAvg + 47
                                        DecimalFormat("#.##").format(correctedAvg)
                                    } else {
                                        "0.0"
                                    }

                                    // Robot state
                                    val robotStateText = getRobotStateText(state)
                                    val robotStateColor = getRobotStateColor(state)

                                    // Distance display
                                    val distanceText = if (distance >= 999) "Clear" else "${distance} cm"
                                    val distanceColor = when {
                                        distance >= 999 -> Color.GREEN
                                        distance > 50 -> Color.YELLOW
                                        else -> Color.RED
                                    }

                                    // MQTT status
                                    val mqttText = if (mqttConnected) "Connected" else "Disconnected"
                                    val mqttColor = if (mqttConnected) Color.GREEN else Color.RED

                                    // Uptime formatting
                                    val uptimeText = formatUptime(uptime)

                                    Log.d("Parsed Data", "Avg: $roundedAvg, Level: $level, State: $robotStateText, Distance: $distance")

                                    // Vibrate on sound level change
                                    if (level != lastSoundLevel && level >= 2) {
                                        VibrationUtil.triggerVibration(this@HomeActivity, 100)
                                        lastSoundLevel = level
                                    }

                                    runOnUiThread {
                                        soundTxt.text = "$roundedAvg dB"
                                        soundLevelTxt.text = soundLevel
                                        soundLevelTxt.setTextColor(textColor)

                                        robotStateTxt.text = robotStateText
                                        robotStateTxt.setTextColor(robotStateColor)

                                        distanceTxt.text = distanceText
                                        distanceTxt.setTextColor(distanceColor)

                                        mqttStatusTxt.text = mqttText
                                        mqttStatusTxt.setTextColor(mqttColor)

                                        uptimeTxt.text = uptimeText
                                        uptimeTxt.setTextColor(Color.BLACK)
                                    }

                                } catch (e: Exception) {
                                    Log.e("JSON Parse Error", "Error parsing JSON: ${e.message}")
                                    runOnUiThread {
                                        soundTxt.text = "Parse Error"
                                        soundLevelTxt.text = "Data Error"
                                        soundLevelTxt.setTextColor(Color.RED)
                                    }
                                }
                            } else {
                                Log.e("HTTP Error", "Empty response from server.")
                                runOnUiThread {
                                    soundTxt.text = "Empty Response"
                                    soundLevelTxt.text = "Server Error"
                                    soundLevelTxt.setTextColor(Color.RED)
                                }
                            }
                        }
                    })
                } else {
                    runOnUiThread {
                        Log.w("Wi-Fi Connection", "Not connected to SoundBot-AP")
                        resetDisplay()
                    }
                }

                // Schedule next polling
                pollingRunnable?.let {
                    handler.postDelayed(it, pollingInterval)
                }
            }
        }

        // Start polling
        pollingRunnable?.let {
            handler.post(it)
        }
    }

    private fun stopPolling() {
        pollingRunnable?.let {
            handler.removeCallbacks(it)
        }
        pollingRunnable = null
    }
}

object VibrationUtil {
    fun triggerVibration(context: Context, duration: Long = 150) {
        playClickSound(context)
        val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator?

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val vibrationEffect = VibrationEffect.createOneShot(
                duration,
                VibrationEffect.DEFAULT_AMPLITUDE
            )
            vibrator?.cancel()
            vibrator?.vibrate(vibrationEffect)
        } else {
            @Suppress("DEPRECATION")
            vibrator?.vibrate(duration)
        }
    }

    private fun playClickSound(context: Context) {
        try {
            val mediaPlayer = MediaPlayer.create(context, R.raw.click)
            mediaPlayer?.setOnCompletionListener { mp ->
                mp.release()
            }
            mediaPlayer?.start()
        } catch (e: Exception) {
            Log.w("VibrationUtil", "Could not play click sound: ${e.message}")
        }
    }
}