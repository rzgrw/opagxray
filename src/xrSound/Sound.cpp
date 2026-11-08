#include "stdafx.h"

#include "SoundRender_CoreA.h"

XRSOUND_API u32 snd_device_id = u32(-1);

ISoundScene* DefaultSoundScene{};

namespace
{
// CreateDevicesList runs as a startup task that may start before
// Core.Initialize has filled Core.Params; the process command line is always set.
bool HasStartupParam(pcstr name)
{
#ifdef XR_PLATFORM_WINDOWS
    if (strstr(GetCommandLineA(), name))
        return true;
#endif
    return Core.Params && strstr(Core.Params, name);
}
} // namespace

void CSoundManager::CreateDevicesList()
{
    ZoneScoped;

    // -agent_background instances are muted: never open an audio device.
    static bool noSound = HasStartupParam("-nosound") || HasStartupParam("-agent_background");

    SoundRender = xr_new<CSoundRender_CoreA>(*this);

    if (!noSound)
        SoundRender->_initialize_devices_list();

    if (!SoundRender->bPresent)
        soundDevices.emplace_back(nullptr, -1);

    GEnv.Sound = SoundRender;
}

void CSoundManager::Create()
{
    ZoneScoped;

    if (SoundRender->bPresent)
    {
        env_load();
        SoundRender->_initialize();
    }
}

void CSoundManager::Destroy()
{
    ZoneScoped;

    GEnv.Sound = nullptr;

    SoundRender->_clear();
    xr_delete(SoundRender);

    env_unload();

    for (auto& token : soundDevices)
    {
        pstr tokenName = const_cast<pstr>(token.name);
        xr_free(tokenName);
    }
    soundDevices.clear();
}

bool CSoundManager::IsSoundEnabled() const
{
    return SoundRender && SoundRender->bPresent;
}

void CSoundManager::env_load()
{
    string_path fn;
    if (FS.exist(fn, "$game_data$", SNDENV_FILENAME))
    {
        soundEnvironment = xr_new<SoundEnvironment_LIB>();
        soundEnvironment->Load(fn);
    }
}

void CSoundManager::env_unload()
{
    if (soundEnvironment)
        soundEnvironment->Unload();
    xr_delete(soundEnvironment);
}

SoundEnvironment_LIB* CSoundManager::get_env_library() const
{
    return soundEnvironment;
}

void CSoundManager::refresh_env_library()
{
    env_unload();
    env_load();
    SoundRender->env_apply();
}
