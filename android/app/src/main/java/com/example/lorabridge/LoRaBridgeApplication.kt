package com.example.lorabridge

import android.app.Application
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import com.example.lorabridge.data.ble.BleRepository
import dagger.hilt.android.HiltAndroidApp
import javax.inject.Inject

/**
 * Application class with Hilt dependency injection and lifecycle awareness
 */
@HiltAndroidApp
class LoRaBridgeApplication : Application(), DefaultLifecycleObserver {

    @Inject
    lateinit var bleRepository: BleRepository

    override fun onCreate() {
        super<Application>.onCreate()
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
    }

    override fun onStart(owner: LifecycleOwner) {
        // App moved to foreground
        bleRepository.resumeScan()
    }

    override fun onStop(owner: LifecycleOwner) {
        // App moved to background
        bleRepository.pauseScan()
    }
}
