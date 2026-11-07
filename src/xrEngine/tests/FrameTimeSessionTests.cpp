#include "xrEngine/FrameTimeSession.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static std::atomic<size_t> allocationCount{0};
void* operator new(size_t size)
{
    ++allocationCount;
    if (void* p=std::malloc(size?size:1)) return p;
    std::abort();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

using namespace xray::frametime;
namespace
{
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)
uint64_t ticks = 0, reads = 0;
uint64_t Clock(void*) { ++reads; return ticks; }
bool FailWrite(FILE* file, const CFrameTimeCollector&, const FrameTimeMetadata&)
{ std::fputs("partial",file); return false; }
}
int main(int argc, char** argv)
{
    CFrameTimeSession session(&Clock);
    const auto disabledAllocations=allocationCount.load();
    for (int i=0;i<10;++i) { session.BeginLoop(0); FrameTimeScope idle(session,FrameTimePhase::Main); }
    CHECK(reads==0);
    CHECK(allocationCount.load()==disabledAllocations);
    FrameTimeMetadata meta;
    meta.Set(FrameTimeMetadata::SourceBuild,"literal-cpp-fixture");
    meta.Set(FrameTimeMetadata::Renderer,"GL");
    meta.Set(FrameTimeMetadata::ContentManifest,"fixture-content");
    meta.Set(FrameTimeMetadata::Overlay,"fixture-overlay");
    meta.Set(FrameTimeMetadata::RenderResolution,"1280x720");
    meta.Set(FrameTimeMetadata::DrawableResolution,"1280x720");
    meta.Set(FrameTimeMetadata::Vsync,"off");
    meta.Set(FrameTimeMetadata::FrameLimit,"0");
    meta.Set(FrameTimeMetadata::SDLVersion,"test-sdl");
    meta.Set(FrameTimeMetadata::AllocatorBuild,"test-standard");
    CHECK(!meta.Set(FrameTimeMetadata::Renderer,"bad\nvalue"));
    CHECK(session.Control("perf.start","16 0",&meta).ok);
    CHECK(!session.Control("perf.export","").ok);
    ticks=100;session.BeginLoop(10);
    { FrameTimeScope main(session,FrameTimePhase::Main);
      ticks=110; { FrameTimeScope present(session,FrameTimePhase::Present); ticks=120; }
      ticks=130;
    }
    ticks=200;session.EndLoop(11);session.BeginLoop(11);
    // A worker must never touch the primary-thread recorder or clock.
    const auto readCount=reads;
    std::thread worker([&]{ FrameTimeScope scope(session,FrameTimePhase::Main); });worker.join();
    CHECK(reads==readCount);
    { FrameTimeScope stale(session,FrameTimePhase::Render);
      CHECK(session.Control("perf.stop","").ok);
      CHECK(!session.Control("perf.export","").ok);
      ticks=300;session.BeginLoop(12);
      CHECK(session.Collector().Samples().size==1);
      CHECK(session.Collector().Samples().data[0].Phase(FrameTimePhase::Present).duration_ns==10);
      CHECK(session.Control("perf.reset","").ok);
      CHECK(session.Control("perf.start","16 0",&meta).ok);
      ticks=400;session.BeginLoop(20);
    } // stale render end must not underflow the new capture
    ticks=500;session.EndLoop(21);session.BeginLoop(21);
    CHECK(session.Collector().GetStatus().invalid_loops==0);
    CHECK(session.Control("perf.stop","").ok);
    ticks=600;session.BeginLoop(22);
    CHECK(session.NeedsShutdownWarning());
    char temp[]="/tmp/xray-frame-session-XXXXXX";
    char* root=mkdtemp(temp);CHECK(root);
    auto fail=session.Export(root,&FailWrite);CHECK(!fail.ok);
    CHECK(session.Collector().Samples().size==1);
    CHECK(session.NeedsShutdownWarning());
    char unsafe[]="/tmp/xray-frame-unsafe-XXXXXX";
    char* unsafeRoot=mkdtemp(unsafe);CHECK(unsafeRoot);
    const std::string unsafePerf=std::string(unsafeRoot)+"/perf";
    CHECK(symlink(root,unsafePerf.c_str())==0);
    CHECK(!session.Export(unsafeRoot).ok);
    CHECK(!session.Export("/does-not-exist/xray-appdata").ok);
    // Deliberate generated-name collision must retain the existing bytes.
    const std::string collision=std::string(root)+"/perf/capture-"+std::to_string(getpid())+"-2.csv";
    FILE* sentinel=std::fopen(collision.c_str(),"w");CHECK(sentinel);
    if(sentinel) { std::fputs("sentinel",sentinel);std::fclose(sentinel); }
    auto good=session.Export(root);CHECK(good.ok);
    sentinel=std::fopen(collision.c_str(),"r");CHECK(sentinel);
    if(sentinel) { char bytes[16]{};CHECK(std::fread(bytes,1,8,sentinel)==8);CHECK(std::strcmp(bytes,"sentinel")==0);std::fclose(sentinel); }
    auto second=session.Export(root);CHECK(second.ok);CHECK(std::strcmp(good.text,second.text)!=0);
    CHECK(!session.NeedsShutdownWarning());
    const std::string perf=std::string(root)+"/perf";
    DIR* dir=opendir(perf.c_str());CHECK(dir);
    int files=0;
    if(dir) { while(auto* ent=readdir(dir)) if(ent->d_name[0]!='.') ++files;closedir(dir); }
    CHECK(files==3); // failed partial output removed; both successful exports and preexisting sentinel retained
    CHECK(!session.Control("perf.export","../escape").ok);
    CHECK(session.Control("perf.status","").ok);
    CHECK(session.Control("perf.reset","").ok);
    CHECK(session.Control("perf.start","x 0",&meta).ok==false);
    CHECK(session.Control("perf.start","1 2 3",&meta).ok==false);
    CHECK(session.Control("perf.start","16 0",&meta).ok);
    ticks=1000;session.BeginLoop(30);
    CHECK(!session.Control("perf.status","").ok); // control overhead cannot become a valid sample
    ticks=1100;session.BeginLoop(31);
    CHECK(session.Collector().Samples().size==0);
    CHECK(session.Collector().GetStatus().invalid_loops==1);
    CHECK(session.NeedsShutdownWarning());
    CHECK(session.Control("perf.stop","").ok);ticks=1200;session.BeginLoop(32);
    CHECK(session.Control("perf.reset","").ok);
    // A stale same-phase end cannot pop a fresh same-phase scope after restart.
    CHECK(session.Control("perf.start","16 0",&meta).ok);
    ticks=1500;session.BeginLoop(35);
    auto* stale=new FrameTimeScope(session,FrameTimePhase::Render);
    CHECK(session.Control("perf.stop","").ok);ticks=1510;session.BeginLoop(36);
    CHECK(session.Control("perf.reset","").ok);
    CHECK(session.Control("perf.start","16 0",&meta).ok);ticks=1520;session.BeginLoop(37);
    {
        FrameTimeScope fresh(session,FrameTimePhase::Render);
        const auto before=reads;delete stale;CHECK(reads==before);
        ticks=1530;
    }
    ticks=1540;session.BeginLoop(38);
    CHECK(session.Collector().Samples().data[0].Phase(FrameTimePhase::Render).calls==1);
    CHECK(session.Collector().GetStatus().invalid_loops==0);
    CHECK(session.Control("perf.stop","").ok);ticks=1550;session.BeginLoop(39);
    CHECK(session.Control("perf.reset","").ok);
    // Presentation changes, even if later restored, make provenance unknown.
    CHECK(session.Control("perf.start","16 0",&meta).ok);ticks=1600;session.BeginLoop(40);
    session.ObserveDimensions(800,600,false);session.ObserveDimensions(1280,720,false);
    session.ObserveVsync(1);session.ObserveVsync(0);
    ticks=1700;session.BeginLoop(41);CHECK(session.Control("perf.stop","").ok);
    ticks=1800;session.BeginLoop(42);
    const auto changed=session.Export(root);CHECK(changed.ok);
    std::string relative=changed.text;relative=relative.substr(5,relative.find(' ')-5);
    FILE* changedFile=std::fopen((std::string(root)+"/"+relative).c_str(),"r");CHECK(changedFile);
    if(changedFile)
    {
        char contents[8192]{};std::fread(contents,1,sizeof(contents)-1,changedFile);std::fclose(changedFile);
        CHECK(std::strstr(contents,"# render_resolution=unknown\n"));
        CHECK(std::strstr(contents,"# vsync=unknown\n"));
    }
    CHECK(session.Control("perf.reset","").ok);
    { // Active destruction is local cleanup, with no implicit clock/stop/export.
        const auto before=reads;
        CFrameTimeSession abandoned(&Clock);CHECK(abandoned.Control("perf.start","1 0").ok);
        CHECK(abandoned.NeedsShutdownWarning());CHECK(reads==before);
    }
    // Literal production writer -> strict Python reporter fixture, no Python-written CSV.
    CHECK(session.Control("perf.start","8 0",&meta).ok);
    ticks=2000;session.BeginLoop(40);
    { FrameTimeScope render(session,FrameTimePhase::Render); ticks=2010;
      { FrameTimeScope present(session,FrameTimePhase::Present); ticks=2020; }
      ticks=2030;
    }
    session.EndLoop(42);ticks=2100;session.BeginLoop(42);
    CHECK(session.Control("perf.stop","").ok);ticks=2200;session.BeginLoop(43);
    if(argc>1) { FILE* f=std::fopen(argv[1],"w");CHECK(f); if(f){CHECK(WriteFrameTimeCapture(f,session.Collector(),meta));CHECK(std::fclose(f)==0);} }
    std::printf("%s: session control, generations, threads, export retention and literal writer\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
