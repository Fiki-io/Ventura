package com.example

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.example.ui.VenturaAppShell
import com.example.ui.theme.MyApplicationTheme
import com.example.ui.viewmodel.VenturaViewModel

class MainActivity : ComponentActivity() {

    private val venturaViewModel: VenturaViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MyApplicationTheme {
                VenturaAppShell(viewModel = venturaViewModel)
            }
        }
    }
}
