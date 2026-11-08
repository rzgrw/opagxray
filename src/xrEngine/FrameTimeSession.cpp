#include "FrameTimeSession.h"
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <cerrno>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <string>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xray::frametime
{
namespace
{
constexpr const char* MetadataKeys[] = {"source_build","renderer","content_manifest","overlay",
    "render_resolution","drawable_resolution","vsync","frame_limit","sdl_version","allocator_build"};
constexpr const char* PhaseNames[] = {"main","render","present","worker_wait","limiter","inactive_sleep",
    "not_ready_sleep","device_sleep","load_draw","fence_wait","occlusion_wait"};
constexpr const char* StateNames[] = {"disabled","armed","capturing","saturated","stop_pending","stopped"};
FrameTimeReply Reply(bool ok, const char* text)
{
    FrameTimeReply result; result.ok = ok;
    std::snprintf(result.text,sizeof(result.text),"%s",text);
    return result;
}
const char* ResultName(FrameTimeResult result)
{
    switch (result)
    {
    case FrameTimeResult::Ok: return "ok";
    case FrameTimeResult::AllocationFailed: return "allocation_failed";
    case FrameTimeResult::InvalidCapacity: return "invalid_capacity";
    case FrameTimeResult::InvalidWarmup: return "invalid_warmup";
    case FrameTimeResult::AlreadyActive: return "already_active";
    case FrameTimeResult::ResetRequired: return "reset_required";
    default: return "invalid_state";
    }
}
bool UnsignedArgument(const char*& text, uint64_t& value)
{
    while (*text == ' ') ++text;
    if (*text < '0' || *text > '9') return false;
    value = 0;
    while (*text >= '0' && *text <= '9')
    {
        const unsigned digit = unsigned(*text++ - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return *text == 0 || *text == ' ';
}
}

FrameTimeMetadata::FrameTimeMetadata()
{
    for (auto& value : values) std::strcpy(value,"unknown");
}
bool FrameTimeMetadata::Set(Field field, const char* value)
{
    if (field < 0 || field >= Count || !value || !*value || std::strlen(value) >= sizeof(values[0])) return false;
    bool nonspace = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    {
        if (*p < 32 || *p > 126) return false;
        nonspace |= *p != ' ';
    }
    if (!nonspace) return false;
    std::strcpy(values[field],value);
    return true;
}

bool WriteFrameTimeCapture(FILE* file, const CFrameTimeCollector& collector, const FrameTimeMetadata& metadata)
{
    const auto s = collector.GetStatus();
    if (!file || s.state != FrameTimeState::Stopped) return false;
    if (std::fprintf(file,"# xray-frame-time-v1\n# state=stopped\n# capacity=%zu\n# warmup_loops=%u\n"
        "# warmup_observed=%u\n# stored_loops=%" PRIu64 "\n# dropped_loops=%" PRIu64 "\n# invalid_loops=%" PRIu64
        "\n# truncated=%s\n# capture_start_ns=%" PRIu64 "\n# capture_end_ns=%" PRIu64
        "\n# clock_is_steady=%s\n# clock_period_ns=%.17g\n", s.capacity,s.warmup_loops,s.warmup_observed,
        s.stored_loops,s.dropped_loops,s.invalid_loops,s.truncated?"true":"false",s.capture_start_ns,s.capture_end_ns,
        std::chrono::steady_clock::is_steady?"true":"false",
        1.e9 * double(std::chrono::steady_clock::period::num) / double(std::chrono::steady_clock::period::den)) < 0) return false;
    for (size_t i=0;i<FrameTimeMetadata::Count;++i)
        if (std::fprintf(file,"# %s=%s\n",MetadataKeys[i],metadata.values[i])<0) return false;
    if (std::fputs("loop_seq,start_ns,wall_ns,engine_frame_begin,engine_frame_end,flags",file)<0) return false;
    for (const char* phase : PhaseNames)
        if (std::fprintf(file,",%s_calls,%s_ns",phase,phase)<0) return false;
    if (std::fputc('\n',file)==EOF) return false;
    const auto samples=collector.Samples();
    for(size_t i=0;i<samples.size;++i)
    {
        const auto& r=samples.data[i];
        if (std::fprintf(file,"%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%u",
            r.loop_seq,r.start_ns,r.wall_ns,r.engine_frame_begin,r.engine_frame_end,r.flags)<0) return false;
        for(const auto& phase:r.phases)
        {
            if (std::fprintf(file,",%u,",phase.calls)<0) return false;
            if (phase.calls && std::fprintf(file,"%" PRIu64,phase.duration_ns)<0) return false;
        }
        if (std::fputc('\n',file)==EOF) return false;
    }
    return !std::ferror(file);
}

CFrameTimeSession::CFrameTimeSession(CFrameTimeCollector::ClockAdapter clock)
    : m_owner(std::this_thread::get_id()), m_collector(clock,nullptr,nullptr,nullptr) {}
CFrameTimeSession::~CFrameTimeSession() = default;
void CFrameTimeSession::AdvanceGeneration()
{
    m_recordScopes = false;
    if (m_generation == std::numeric_limits<uint64_t>::max())
    {
        m_generationExhausted = true;
        m_collector.Stop();
        return;
    }
    ++m_generation;
}
void CFrameTimeSession::BeginLoop(uint32_t engineFrame)
{
    if (!Running()) return;
    AdvanceGeneration();
    m_collector.BeginLoop();
    m_frameBegin = engineFrame;
    const auto state = m_collector.GetStatus().state;
    m_running.store(state != FrameTimeState::Stopped && state != FrameTimeState::Disabled, std::memory_order_relaxed);
    m_recordScopes = Running() && !m_generationExhausted && state == FrameTimeState::Capturing;
}
#ifdef XRAY_OVERHEAD_PROBE
bool CFrameTimeSession::ExperimentalCloseAt(uint64_t closingStamp)
{
    if (!Running() || std::this_thread::get_id() != m_owner) return false;
    const auto close = m_collector.BeginLoopAt(closingStamp);
    const auto stop = m_collector.Stop();
    const auto finish = m_collector.BeginLoopAt(closingStamp);
    AdvanceGeneration();
    m_recordScopes = false;
    m_running.store(false, std::memory_order_relaxed);
    return close == FrameTimeResult::Ok && stop == FrameTimeResult::Ok && finish == FrameTimeResult::Ok;
}
#endif
void CFrameTimeSession::EndLoop(uint32_t engineFrame)
{
    if (Running()) m_collector.SetEngineFrameRange(m_frameBegin,engineFrame);
}
void CFrameTimeSession::RejectMeasuredControl()
{
    if (Running()) m_collector.DiscardCurrentLoop();
}
FrameTimeReply CFrameTimeSession::Status() const
{
    const auto s=m_collector.GetStatus();
    FrameTimeReply reply;reply.ok=true;
    std::snprintf(reply.text,sizeof(reply.text),"state=%s capacity=%zu stored_loops=%" PRIu64
        " warmup_loops=%u warmup_observed=%u dropped_loops=%" PRIu64 " invalid_loops=%" PRIu64
        " truncated=%s has_samples=%s first_loop_seq=%" PRIu64 " last_loop_seq=%" PRIu64
        " capture_start_ns=%" PRIu64 " capture_end_ns=%" PRIu64,
        StateNames[static_cast<size_t>(s.state)],s.capacity,s.stored_loops,s.warmup_loops,s.warmup_observed,
        s.dropped_loops,s.invalid_loops,s.truncated?"true":"false",s.has_samples?"true":"false",
        s.first_loop_seq,s.last_loop_seq,s.capture_start_ns,s.capture_end_ns);
    return reply;
}
FrameTimeReply CFrameTimeSession::Control(const char* verb,const char* payload,const FrameTimeMetadata* metadata)
{
    const auto state=m_collector.GetStatus().state;
    const bool active=state==FrameTimeState::Armed || state==FrameTimeState::Capturing || state==FrameTimeState::Saturated;
    if (active && std::strcmp(verb,"perf.stop")!=0)
    {
        RejectMeasuredControl();
        return Reply(false,"capture_active: only perf.stop allowed; control loop discarded");
    }
    FrameTimeResult result=FrameTimeResult::InvalidState;
    if (std::strcmp(verb,"perf.start")==0)
    {
        if (m_generationExhausted) return Reply(false,"generation_exhausted");
        uint64_t capacity=CFrameTimeCollector::DefaultCapacity,warmup=0;
        const char* p=payload;
        while (*p==' ') ++p;
        if (*p && !UnsignedArgument(p,capacity)) return Reply(false,"invalid_arguments");
        while (*p==' ') ++p;
        if (*p && !UnsignedArgument(p,warmup)) return Reply(false,"invalid_arguments");
        while (*p==' ') ++p;
        if (*p || capacity>CFrameTimeCollector::MaxCapacity || warmup>CFrameTimeCollector::MaxWarmupLoops)
            return Reply(false,"invalid_arguments");
        result=m_collector.Start(size_t(capacity),uint32_t(warmup));
        if(result==FrameTimeResult::Ok)
        {
            AdvanceGeneration();m_running.store(true,std::memory_order_relaxed);m_haveCapture=true;m_exported=false;
            m_metadata=metadata?*metadata:FrameTimeMetadata{};
            m_unknownMetadata=0;
            std::sscanf(m_metadata.values[FrameTimeMetadata::RenderResolution],"%ux%u",&m_dimensions[0],&m_dimensions[1]);
            std::sscanf(m_metadata.values[FrameTimeMetadata::DrawableResolution],"%ux%u",&m_dimensions[2],&m_dimensions[3]);
            std::sscanf(m_metadata.values[FrameTimeMetadata::FrameLimit],"%u",&m_limit);
            m_vsync=std::strcmp(m_metadata.values[FrameTimeMetadata::Vsync],"off")==0?0:
                std::strcmp(m_metadata.values[FrameTimeMetadata::Vsync],"adaptive")==0?-1:1;
        }
    }
    else
    {
        if (*payload) { RejectMeasuredControl();return Reply(false,"unexpected_payload"); }
        if (std::strcmp(verb,"perf.stop")==0)
        {
            result=m_collector.Stop();
            if(result==FrameTimeResult::Ok) AdvanceGeneration();
        }
        else if (std::strcmp(verb,"perf.reset")==0)
        {
            result=m_collector.Reset();
            if(result==FrameTimeResult::Ok)
            { AdvanceGeneration();m_running.store(false,std::memory_order_relaxed);m_haveCapture=false;m_exported=false; }
        }
        else if (std::strcmp(verb,"perf.status")==0) return Status();
        else if (std::strcmp(verb,"perf.export")==0)
            return Reply(state==FrameTimeState::Stopped,state==FrameTimeState::Stopped?"ready":"not_stopped");
        else return Reply(false,"unknown_perf_verb");
    }
    if(result!=FrameTimeResult::Ok) return Reply(false,ResultName(result));
    return Status();
}

FrameTimeReply CFrameTimeSession::Export(const char* appdata,Writer writer)
{
    if (m_collector.GetStatus().state!=FrameTimeState::Stopped) return Reply(false,"not_stopped");
#ifdef _WIN32
    // Same contract as POSIX: no reparse-point (symlink/junction) traversal for the
    // appdata and perf directories, exclusive create, never overwrite, delete only
    // our own failed output. Directory handles opened without FILE_SHARE_DELETE pin
    // both directories (no rename/replace) until the capture file is closed.
    std::string base(appdata);
    while(!base.empty() && (base.back()=='\\' || base.back()=='/')) base.pop_back();
    const auto openDir=[](const std::string& path)
    {
        HANDLE h=::CreateFileA(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        if(h!=INVALID_HANDLE_VALUE && (!::GetFileInformationByHandle(h,&info) ||
            !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)))
        { ::CloseHandle(h);h=INVALID_HANDLE_VALUE; }
        return h;
    };
    const HANDLE root=openDir(base);
    if(root==INVALID_HANDLE_VALUE) return Reply(false,"appdata_open_failed");
    const std::string perf=base+"\\perf";
    if(!::CreateDirectoryA(perf.c_str(),nullptr) && ::GetLastError()!=ERROR_ALREADY_EXISTS)
    { ::CloseHandle(root);return Reply(false,"perf_directory_failed"); }
    const HANDLE dir=openDir(perf);::CloseHandle(root);
    if(dir==INVALID_HANDLE_VALUE) return Reply(false,"perf_directory_failed");
    static uint64_t serial=0; // primary thread, process-unique with PID; CREATE_NEW is authoritative
    HANDLE out=INVALID_HANDLE_VALUE;char name[128]{};std::string target;
    for(int tries=0;tries<128;++tries)
    {
        if(serial==std::numeric_limits<uint64_t>::max()) break;
        std::snprintf(name,sizeof(name),"capture-%lu-%" PRIu64 ".csv",::GetCurrentProcessId(),++serial);
        target=perf+"\\"+name;
        out=::CreateFileA(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(out!=INVALID_HANDLE_VALUE || ::GetLastError()!=ERROR_FILE_EXISTS) break;
    }
    if(out==INVALID_HANDLE_VALUE) { ::CloseHandle(dir);return Reply(false,"exclusive_create_failed"); }
    const int fd=::_open_osfhandle(reinterpret_cast<intptr_t>(out),_O_WRONLY|_O_BINARY); // binary: same bytes as POSIX
    FILE* file=fd>=0?::_fdopen(fd,"wb"):nullptr;
    bool written=false;
    if(file)
    {
        auto metadata=m_metadata;
        for(int i=0;i<FrameTimeMetadata::Count;++i)
            if(m_unknownMetadata & (1u<<i)) metadata.Set(static_cast<FrameTimeMetadata::Field>(i),"unknown");
        written=writer(file,m_collector,metadata);
        if(std::fflush(file)!=0) written=false;
        if(std::fclose(file)!=0) written=false;
    }
    else if(fd>=0) ::_close(fd);
    else ::CloseHandle(out);
    if(!written) ::DeleteFileA(target.c_str()); // only this newly created, failed output
    ::CloseHandle(dir);
    if(!written) return Reply(false,"write_failed: capture retained");
    m_exported=true;
    auto result=Status();
    char summary[sizeof(result.text)];std::strcpy(summary,result.text);
    std::snprintf(result.text,sizeof(result.text),"path=perf/%s %.1700s",name,summary);
    return result;
#else
    const int root=::open(appdata,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(root<0) return Reply(false,"appdata_open_failed");
    if(::mkdirat(root,"perf",0700)!=0 && errno!=EEXIST)
    { ::close(root);return Reply(false,"perf_directory_failed"); }
    const int dir=::openat(root,"perf",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);::close(root);
    if(dir<0) return Reply(false,"perf_directory_failed");
    static uint64_t serial=0; // primary thread, process-unique with PID; O_EXCL is authoritative
    int fd=-1;char name[128]{};
    for(int tries=0;tries<128;++tries)
    {
        if(serial==std::numeric_limits<uint64_t>::max()) break;
        std::snprintf(name,sizeof(name),"capture-%ld-%" PRIu64 ".csv",long(::getpid()),++serial);
        fd=::openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0 || errno!=EEXIST) break;
    }
    if(fd<0) { ::close(dir);return Reply(false,"exclusive_create_failed"); }
    FILE* file=::fdopen(fd,"w");
    bool written=false;
    if(file)
    {
        auto metadata=m_metadata;
        for(int i=0;i<FrameTimeMetadata::Count;++i)
            if(m_unknownMetadata & (1u<<i)) metadata.Set(static_cast<FrameTimeMetadata::Field>(i),"unknown");
        written=writer(file,m_collector,metadata);
        if(std::fflush(file)!=0) written=false;
        if(std::fclose(file)!=0) written=false;
    }
    else ::close(fd);
    if(!written) ::unlinkat(dir,name,0); // only this newly created, failed output
    ::close(dir);
    if(!written) return Reply(false,"write_failed: capture retained");
    m_exported=true;
    auto result=Status();
    char summary[sizeof(result.text)];std::strcpy(summary,result.text);
    std::snprintf(result.text,sizeof(result.text),"path=perf/%s %.1700s",name,summary);
    return result;
#endif
}
void CFrameTimeSession::ObserveDimensions(uint32_t width,uint32_t height,bool drawable)
{
    if (!Running()) return;
    const auto field=drawable?FrameTimeMetadata::DrawableResolution:FrameTimeMetadata::RenderResolution;
    const unsigned offset=drawable?2:0;
    if(width!=m_dimensions[offset] || height!=m_dimensions[offset+1]) m_unknownMetadata |= 1u<<field;
}
void CFrameTimeSession::ObserveFrameLimit(uint32_t limit)
{
    if (Running() && limit!=m_limit) m_unknownMetadata |= 1u<<FrameTimeMetadata::FrameLimit;
}
void CFrameTimeSession::ObserveVsync(int interval)
{
    if (Running() && interval!=m_vsync) m_unknownMetadata |= 1u<<FrameTimeMetadata::Vsync;
}
}
