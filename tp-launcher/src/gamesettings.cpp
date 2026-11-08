// gamesettings.cpp - see gamesettings.h for the defaults policy.
#include "gamesettings.h"

#include <QCoreApplication>
#include <QSettings>

QSettings *settingsObject()
{
    // Org/app names are set in main.cpp from the active profile, so
    // each game build gets its own ini file (~/.config/HouseRecomp/
    // <launcher name>.ini) and settings never bleed across games.
    // IniFormat explicitly: the file stays hand-editable.
    static QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("HouseRecomp"),
                              QCoreApplication::applicationName());
    return &settings;
}

GameSettings GameSettings::load()
{
    GameSettings s; // defaults from the struct - the single source of truth
    QSettings *q = settingsObject();
    q->beginGroup(QStringLiteral("settings"));
    // Graphics
    s.resolutionScale = q->value(QStringLiteral("resolution_scale"), s.resolutionScale).toInt();
    s.upscaling       = q->value(QStringLiteral("upscaling"), s.upscaling).toString();
    s.swapPostEffect  = q->value(QStringLiteral("swap_post_effect"), s.swapPostEffect).toString();
    s.anisotropic     = q->value(QStringLiteral("anisotropic_override"), s.anisotropic).toInt();
    s.native2xMsaa    = q->value(QStringLiteral("native_2x_msaa"), s.native2xMsaa).toBool();
    s.asyncShaderCompilation = q->value(QStringLiteral("async_shader_compilation"), s.asyncShaderCompilation).toBool();
    s.vsync           = q->value(QStringLiteral("vsync"), s.vsync).toBool();
    s.frameLimit      = q->value(QStringLiteral("frame_limit"), s.frameLimit).toInt();
    s.windowed        = q->value(QStringLiteral("windowed"), s.windowed).toBool();
    // Audio
    s.audioMute       = q->value(QStringLiteral("audio_mute"), s.audioMute).toBool();
    // Storage & logs
    s.userDataRoot    = q->value(QStringLiteral("user_data_root"), s.userDataRoot).toString();
    s.logDir          = q->value(QStringLiteral("log_dir"), s.logDir).toString();
    s.logLevel        = q->value(QStringLiteral("log_level"), s.logLevel).toString();
    // Advanced
    s.sparseSharedMemory = q->value(QStringLiteral("vulkan_sparse_shared_memory"), s.sparseSharedMemory).toBool();
    s.asyncSkipIncompleteFrames = q->value(QStringLiteral("vulkan_async_skip_incomplete_frames"), s.asyncSkipIncompleteFrames).toBool();
    s.userLanguage    = q->value(QStringLiteral("user_language"), s.userLanguage).toInt();
    s.useTitleUpdate  = q->value(QStringLiteral("use_title_update"), s.useTitleUpdate).toBool();
    s.userCountry     = q->value(QStringLiteral("user_country"), s.userCountry).toInt();
    const int storedVersion = q->value(QStringLiteral("settings_version"), 1).toInt();
    q->endGroup();

    if (storedVersion < kSettingsVersion) {
#if defined(TP_GAME_REALSTEEL)
        // v1 shipped frame_limit = 0 (unlimited) with vsync off, which left
        // the game unpaced: too fast in light scenes, slow motion in heavy
        // ones. Move existing users to the paced defaults once.
        GameSettings defaults;
        s.frameLimit = defaults.frameLimit;
        s.vsync = defaults.vsync;
#endif
        // Drop keys for options the runtime never had.
        q->beginGroup(QStringLiteral("settings"));
        for (const char *dead : {"audio_gain", "audio_highpass_hz", "audio_force_stereo",
                                 "audio_front_only", "audio_maxqframes", "xma_worker",
                                 "vulkan_pipeline_creation_threads"})
            q->remove(QLatin1String(dead));
        q->endGroup();
        s.save();
    }
    return s;
}

void GameSettings::save() const
{
    QSettings *q = settingsObject();
    q->beginGroup(QStringLiteral("settings"));
    // Graphics
    q->setValue(QStringLiteral("resolution_scale"), resolutionScale);
    q->setValue(QStringLiteral("upscaling"), upscaling);
    q->setValue(QStringLiteral("swap_post_effect"), swapPostEffect);
    q->setValue(QStringLiteral("anisotropic_override"), anisotropic);
    q->setValue(QStringLiteral("native_2x_msaa"), native2xMsaa);
    q->setValue(QStringLiteral("async_shader_compilation"), asyncShaderCompilation);
    q->setValue(QStringLiteral("vsync"), vsync);
    q->setValue(QStringLiteral("frame_limit"), frameLimit);
    q->setValue(QStringLiteral("windowed"), windowed);
    // Audio
    q->setValue(QStringLiteral("audio_mute"), audioMute);
    // Storage & logs
    q->setValue(QStringLiteral("user_data_root"), userDataRoot);
    q->setValue(QStringLiteral("log_dir"), logDir);
    q->setValue(QStringLiteral("log_level"), logLevel);
    // Advanced
    q->setValue(QStringLiteral("vulkan_sparse_shared_memory"), sparseSharedMemory);
    q->setValue(QStringLiteral("use_title_update"), useTitleUpdate);
    q->setValue(QStringLiteral("vulkan_async_skip_incomplete_frames"), asyncSkipIncompleteFrames);
    q->setValue(QStringLiteral("user_language"), userLanguage);
    q->setValue(QStringLiteral("user_country"), userCountry);
    q->setValue(QStringLiteral("settings_version"), kSettingsVersion);
    q->endGroup();
    q->sync();
}
