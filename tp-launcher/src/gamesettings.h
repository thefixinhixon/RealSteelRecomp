// gamesettings.h - launcher-wide user settings, persisted via QSettings.
//
// DEFAULTS LIVE HERE, in this struct, and nowhere else. The loader reads
// each key with the struct's own current value as the fallback
// (settings.value(key, s.field)), so the struct, the UI and the file
// can never disagree about a default. (Learned the hard way: a previous
// launcher had one default in the struct and another in the UI struct,
// and FSR silently came up wrong.)
#pragma once

#include <QString>

struct GameSettings {
    // --- Graphics ---
    // Resolution is an integer render-scale multiplier over the game's
    // 720p internal resolution (1 = 720p, 2 = 1440p, 3 = 2160p) - the
    // scheme the shipped 'Splosion Man launcher proved; fractional
    // scales are unproven. The post effect is stored as the runtime's
    // own swap_post_effect value string.
    int     resolutionScale = 1;                       // 1 / 2 / 3
    QString upscaling  = QStringLiteral("FSR");        // FSR / Bilinear
    QString swapPostEffect = QStringLiteral("fxaa");   // none / fxaa / fxaa_extreme
    int     anisotropic = 16;        // anisotropic_override; 0 = game's own setting
    bool    native2xMsaa = false;    // native_2x_msaa
    bool    asyncShaderCompilation = true; // async_shader_compilation
#if defined(TP_GAME_REALSTEEL)
    // Real Steel's game logic advances one step per frame, so game speed
    // IS the frame rate: the runtime's limiter must hold exactly 60.
    // VSync is safe again (house runtime 2026-11): it now only makes host
    // presentation tear-free; it no longer drops a slow frame to 30 fps
    // (which used to run fights at half speed).
    bool    vsync      = true;
    int     frameLimit = 60;   // 60 = correct game speed
#else
    bool    vsync      = true;
    int     frameLimit = 0;    // 0 = unlimited
#endif
    bool    windowed   = false;

    // --- Audio ---
    // The runtime has exactly one audio option. (The old gain / highpass /
    // stereo / queue / XMA-worker settings named flags the runtime does
    // not have and did nothing; they were removed.)
    bool    audioMute  = false; // audio_mute

    // --- Storage & logs ---
    QString userDataRoot;                        // user_data_root; empty = runtime default
    QString logDir;                              // empty = <data root>/logs
    QString logLevel = QStringLiteral("info");   // info / off / debug

    // --- Advanced ---
    bool    sparseSharedMemory = false;        // vulkan_sparse_shared_memory
    bool    asyncSkipIncompleteFrames = false; // vulkan_async_skip_incomplete_frames
    int     userLanguage = 0; // user_language; 0 = runtime default
    int     userCountry  = 0; // user_country; 0 = runtime default
    // Start the title update build (GameProfile::titleUpdateExeName) when
    // a matching title update is installed. Off = always the base game.
    bool    useTitleUpdate = true;    // launcher-only; no runtime flag

    // Bumped when a stored default is wrong for existing users and must be
    // migrated (see GameSettings::load()).
    static constexpr int kSettingsVersion = 2;

    static GameSettings load();
    void save() const;
};

// The one QSettings factory used everywhere (IniFormat, user scope).
class QSettings;
QSettings *settingsObject();
