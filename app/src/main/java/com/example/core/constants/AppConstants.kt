package com.example.core.constants

object AppConstants {
    const val NATIVE_LIBRARY_NAME = "ventura_core"
    const val TARGET_ABI_ARM64 = "arm64-v8a"

    // PE32 & GTA SA v1.0 US Binary Constants
    const val PE_DOS_SIGNATURE: Int = 0x5A4D
    const val PE_NT_SIGNATURE: Int = 0x00004550
    const val PE_MACHINE_I386: Int = 0x014C
    const val PE_OPT_HEADER_MAGIC_PE32: Int = 0x010B

    const val GTASA_IMAGE_BASE: Long = 0x00400000L
    const val GTASA_ENTRY_POINT_RVA: Long = 0x00424570L
    const val GTASA_ENTRY_POINT_VA: Long = 0x00824570L
    const val GTASA_D3D9_DEVICE_PTR_VA: Long = 0x00C97C28L
    const val SAMP_MODULE_BASE_VA: Long = 0x10000000L
    const val VIRTUAL_ARENA_SIZE_BYTES: Int = 0x02000000

    // Standard Game Files
    const val FILE_GTA_SA_EXE = "gta_sa.exe"
    const val FILE_SAMP_DLL = "samp.dll"
    const val FILE_GTA3_IMG_REL = "models/gta3.img"

    // IMG VER2 Archive Constants
    const val IMG_MAGIC_VER2 = "VER2"
    const val IMG_SECTOR_SIZE_BYTES = 2048
    const val IMG_DIRECTORY_ENTRY_BYTES = 32
    const val RW_SECTION_CLUMP: Int = 0x0010
    const val RW_SECTION_TEXDICTIONARY: Int = 0x0016
    const val RW_VERSION_GTASA: Int = 0x1803FFFF

    // SA-MP UDP Query Protocol Constants
    const val SAMP_HEADER_SIZE_BYTES = 11
    const val SAMP_DEFAULT_HOST = "127.0.0.1"
    const val SAMP_DEFAULT_PORT = 7777
    const val SAMP_UDP_TIMEOUT_MS = 1800
    const val SAMP_OPCODE_INFO: Byte = 'i'.code.toByte()
    const val SAMP_OPCODE_RULES: Byte = 'r'.code.toByte()
    const val SAMP_OPCODE_PLAYERS: Byte = 'd'.code.toByte()
    const val SAMP_OPCODE_PING: Byte = 'p'.code.toByte()

    // Windows Virtual-Key Codes forwarded to WndProc & DirectInput8 in C++
    const val VK_LBUTTON: Int = 0x01
    const val VK_RBUTTON: Int = 0x02
    const val VK_BACK: Int = 0x08
    const val VK_TAB: Int = 0x09
    const val VK_RETURN: Int = 0x0D
    const val VK_SHIFT: Int = 0x10
    const val VK_ESCAPE: Int = 0x1B
    const val VK_SPACE: Int = 0x20
    const val VK_A: Int = 0x41
    const val VK_D: Int = 0x44
    const val VK_F: Int = 0x46
    const val VK_G: Int = 0x47
    const val VK_S: Int = 0x53
    const val VK_W: Int = 0x57
    const val VK_F6: Int = 0x75

    // Compose Test Tags
    const val TAG_INPUT_DIR_PATH = "input_directory_path"
    const val TAG_BTN_PICK_FOLDER = "btn_pick_folder"
    const val TAG_BTN_VERIFY_FOLDER = "btn_verify_folder"
    const val TAG_INPUT_NICKNAME = "input_player_nickname"
    const val TAG_INPUT_SERVER_HOST = "input_server_host"
    const val TAG_INPUT_SERVER_PORT = "input_server_port"
    const val TAG_BTN_QUERY_SERVER = "btn_query_samp_server"
    const val TAG_BTN_SAVE_SERVER = "btn_save_server"
    const val TAG_BTN_START_SAMP = "btn_start_samp"
    const val TAG_BTN_START_SP = "btn_start_singleplayer"
    const val TAG_BTN_EXIT_RUNNER = "btn_exit_runner"
}
