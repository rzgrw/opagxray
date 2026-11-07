#include "stdafx.h"
#include "OverheadExperiment.h"
#include "OverheadProbe.h"
#include "OverheadIdentity.h"
#include "IGame_Level.h"
#include "XR_IOConsole.h"
#include <SDL.h>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>

extern int ps_fps_limit;

namespace xray::overhead
{
namespace
{
Probe probe;
uint64_t warmup=0, measured=0, run=0;
char scene[96]{}, save[96]{}, level[96]{}, binary[65]{}, settings[65]{}, machine[96]{};
char appdata[4096]{};
int completion=-1, startFence=-1;
unsigned width=0,height=0;
bool Token(const char* s, bool machineName=false)
{
    if (!*s) return false;
    for (;*s;++s) if (!std::isalnum(static_cast<unsigned char>(*s)) && *s!='_' && *s!='-' && *s!='.' && !(machineName && *s==',')) return false;
    return true;
}
bool Hash(const char* s)
{
    if (std::strlen(s)!=64) return false;
    for (;*s;++s) if (!(*s>='0' && *s<='9') && !(*s>='a' && *s<='f')) return false;
    return true;
}
bool Export()
{
    // Only called once after auto-disarm. Exclusive staging and atomic no-replace
    // publication; a failed or crashed export cannot become an accepted CSV.
    const int root=::open(appdata,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(root<0) return false;
    if(::mkdirat(root,"perf",0700)!=0 && errno!=EEXIST) { ::close(root);return false; }
    const int dir=::openat(root,"perf",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);::close(root);
    if(dir<0) return false;
    char name[96],tmp[100];
    std::snprintf(name,sizeof(name),"overhead-%ld.csv",long(::getpid()));
    std::snprintf(tmp,sizeof(tmp),"%s.tmp",name);
    int fd=::openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0) { ::close(dir);return false; }
    FILE* f=::fdopen(fd,"w");
    if(!f) { ::close(fd);::close(dir);return false; }
    std::fprintf(f,"# xray-overhead-probe-v1\n# role=%s\n# revision=%s\n# probe_core_sha256=%s\n"
        "# callsite_patch_sha256=%s\n# source_snapshot_sha256=%s\n# binary_sha256=%s\n"
        "# scene=%s save=%s level=%s\n# vsync=off frame_limit=501 resolution=%ux%u renderer=renderer_r3\n"
        "# user_ltx_sha256=%s\n# content_manifest=unknown overlay=unknown\n"
        "# clock_is_steady=%s clock_period_ns=%.9Lf\n"
        "# nwarmup=%" PRIu64 " nmeasured=%" PRIu64 " stamps=%zu truncated=false\n# machine=%s run_index=%" PRIu64 "\n",
        XR_OVERHEAD_ROLE,XR_OVERHEAD_REVISION,XR_OVERHEAD_CORE,XR_OVERHEAD_CALLSITE,XR_OVERHEAD_SOURCE,
        binary,scene,save,level,width,height,settings,std::chrono::steady_clock::is_steady?"true":"false",
        1000000000.0L*std::chrono::steady_clock::period::num/std::chrono::steady_clock::period::den,
        warmup,measured,probe.Count(),machine,run);
#if XR_OVERHEAD_HAS_COLLECTOR
    const auto st=Device.FrameTime.Collector().GetStatus();
    uint32_t flags=0; uint64_t notReady=0;
    const auto samples=Device.FrameTime.Collector().Samples();
    for(size_t i=0;i<samples.size;++i)
    {
        flags |= samples.data[i].flags;
        notReady += samples.data[i].Phase(xray::frametime::FrameTimePhase::NotReadySleep).calls;
    }
    std::fprintf(f,"# collector_rows=%" PRIu64 " collector_capacity=%zu collector_warmup=%u collector_invalid=%" PRIu64
        " collector_dropped=%" PRIu64 " collector_truncated=%s collector_flags=%u collector_not_ready=%" PRIu64
        " collector_start_ns=%" PRIu64 " collector_end_ns=%" PRIu64 "\n",
        st.stored_loops,st.capacity,st.warmup_observed,st.invalid_loops,st.dropped_loops,
        st.truncated?"true":"false",flags,notReady,st.capture_start_ns,st.capture_end_ns);
#else
    std::fprintf(f,"# collector_rows=0 collector_capacity=0 collector_warmup=0 collector_invalid=0 collector_dropped=0 "
        "collector_truncated=false collector_flags=0 collector_not_ready=0 collector_start_ns=0 collector_end_ns=0\n");
#endif
    bool valid=true;
    std::fprintf(f,"loop_seq,start_ns,wall_ns\n");
    for(size_t i=0;i+1<probe.Count();++i)
    {
        if(probe.Stamps()[i+1]<=probe.Stamps()[i]) valid=false;
        std::fprintf(f,"%zu,%" PRIu64 ",%" PRIu64 "\n",i,probe.Stamps()[i],probe.Stamps()[i+1]-probe.Stamps()[i]);
    }
    if(std::ferror(f) || std::fflush(f)!=0) valid=false;
    if(std::fclose(f)!=0) valid=false;
    // linkat is an atomic non-overwriting publication on POSIX, unlike rename().
    if(valid && ::linkat(dir,tmp,dir,name,0)!=0) valid=false;
    if(valid) ::unlinkat(dir,tmp,0);
    ::close(dir);
    return valid;
}
}
std::string Control(const std::string& verb,const std::string& payload,bool& ok)
{
    ok=false;
    if(verb=="overhead.ready")
    {
        if(!payload.empty()) return "unexpected_payload";
        char value[512];
        std::snprintf(value,sizeof(value),"ready=%d active=%d paused=%d precache=%u pos=%.6f,%.6f,%.6f dir=%.6f,%.6f,%.6f vsync=%d limit=%d resolution=%ux%u renderer=%s",
            g_pGameLevel && g_pGameLevel->bReady && Device.b_is_Ready && !load_screen_renderer.IsActive(),
            Device.b_is_Active,Device.Paused(),Device.dwPrecacheFrame,
            Device.vCameraPosition.x,Device.vCameraPosition.y,Device.vCameraPosition.z,
            Device.vCameraDirection.x,Device.vCameraDirection.y,Device.vCameraDirection.z,
            SDL_GL_GetSwapInterval(),ps_fps_limit,Device.dwWidth,Device.dwHeight,
            Console && Console->GetString("renderer") ? Console->GetString("renderer") : "unknown");
        ok=true;return value;
    }
    if(verb!="overhead.arm") return "unknown_overhead_verb";
    if(probe.Armed() || probe.Count()) return "one_capture_per_process";
    char extra;
    if(std::sscanf(payload.c_str(),"%" SCNu64 " %" SCNu64 " %" SCNu64 " %95s %95s %95s %64s %64s %95s %c",
        &warmup,&measured,&run,scene,save,level,binary,settings,machine,&extra)!=9 ||
        !Token(scene)||!Token(save)||!Token(level)||!Token(machine,true)||!Hash(binary)||!Hash(settings)||run<1||run>5)
        return "invalid_arguments";
    if(!g_pGameLevel || !g_pGameLevel->bReady || !Device.b_is_Ready || !Device.b_is_Active ||
        Device.Paused() || Device.dwPrecacheFrame || load_screen_renderer.IsActive() ||
        !SDL_GL_GetCurrentContext() || SDL_GL_GetSwapInterval()!=0 || ps_fps_limit!=501 ||
        !Console || !Console->GetString("renderer") || std::strcmp(Console->GetString("renderer"),"renderer_r3")!=0)
        return "scene_not_ready_or_settings_mismatch";
    const char* inherited=std::getenv("XR_OVERHEAD_COMPLETION_FD");
    char tail;
    struct stat st{};
    if(!inherited || std::sscanf(inherited,"%d%c",&completion,&tail)!=1 || completion<3 ||
        ::fstat(completion,&st)!=0 || !S_ISFIFO(st.st_mode)) return "missing_completion_pipe";
    const char* startInherited=std::getenv("XR_OVERHEAD_START_FD");
    if(!startInherited || std::sscanf(startInherited,"%d%c",&startFence,&tail)!=1 || startFence<3 ||
        startFence==completion || ::fstat(startFence,&st)!=0 || !S_ISFIFO(st.st_mode)) return "missing_start_pipe";
    string_path root;FS.update_path(root,"$app_data_root$","");convert_path_separators(root);
    if(std::strlen(root)>=sizeof(appdata)) return "appdata_path_too_long";
    std::strcpy(appdata,root);width=Device.dwWidth;height=Device.dwHeight;
#if XR_OVERHEAD_HAS_COLLECTOR
    if(Device.FrameTime.Collector().GetStatus().state!=xray::frametime::FrameTimeState::Disabled)
        return "collector_not_disabled";
#endif
    if(!probe.Arm(warmup,measured)) return "invalid_counts_or_allocation";
#if XR_OVERHEAD_CAPTURE
    char args[96];std::snprintf(args,sizeof(args),"%" PRIu64 " %" PRIu64,measured+1,warmup);
    if(!Device.FrameTime.Control("perf.start",args).ok)
    {
        probe.CancelBeforeFirstBoundary();
        return "collector_start_failed_process_unusable";
    }
#endif
    ok=true;return "armed";
}
void AwaitRunner()
{
    // The normal bridge response is asynchronous. Keep this control iteration
    // excluded until the runner has actually received it and releases the fence.
    // Nothing polls/reads this pipe at an armed outer boundary.
    pollfd fd{startFence,POLLIN,0};char byte=0;
    if(::poll(&fd,1,30000)!=1 || ::read(startFence,&byte,1)!=1 || byte!='G')
    {
        // Failed handshake cannot yield a valid capture. Normal runner cleanup
        // requests quit; no forced exit or runtime-setting mutation here.
        probe.CancelBeforeFirstBoundary();
#if XR_OVERHEAD_CAPTURE
        Device.FrameTime.Control("perf.stop","");
#endif
        // EOF is an aborted control, never a premature completion notification.
        ::close(completion);completion=-1;
    }
    ::close(startFence);startFence=-1;
}
void Boundary()
{
    if(!probe.Boundary()) return;
    bool valid=true;
#if XR_OVERHEAD_CAPTURE
    valid=Device.FrameTime.ExperimentalCloseAt(probe.Last());
#endif
    valid=Export() && valid;
    const char byte=valid?'1':'0';
    // One completion only, after the final boundary and export. No forced quit.
    const auto ignored=::write(completion,&byte,1);(void)ignored;
    ::close(completion);completion=-1;
}
}
