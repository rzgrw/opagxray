add_library(xrEngine)
include("${CMAKE_SOURCE_DIR}/cmake/OverheadExperiment.cmake")

option(XRAY_AGENT_HTTP "Build live xrSim HTTP provider transport when libcurl is available" ON)
if(XRAY_AGENT_HTTP)
    find_package(CURL QUIET)
endif()

add_library(xrSimCore STATIC
    xrSim/xrSimZoneManifest.h
    xrSim/xrSimZoneManifest.cpp
    xrSim/xrSimZoneState.h
    xrSim/xrSimZoneState.cpp
    xrSim/xrSimActuator.cpp
    xrSim/xrSimActuator.h
    xrSim/xrSimActorIntent.cpp
    xrSim/xrSimActorIntent.h
    xrSim/xrSimActorProvider.cpp
    xrSim/xrSimActorProvider.h
    xrSim/xrSimActorRuntime.cpp
    xrSim/xrSimActorRuntime.h
    xrSim/xrSimActors.cpp
    xrSim/xrSimActors.h
    xrSim/xrSimAgentProvider.cpp
    xrSim/xrSimAgentProvider.h
    xrSim/xrSimAnthropicTransport.cpp
    xrSim/xrSimAnthropicTransport.h
    xrSim/xrSimBridge.cpp
    xrSim/xrSimBridge.h
    xrSim/xrSimNullAgent.cpp
    xrSim/xrSimNullAgent.h
    xrSim/xrSimPackSpawn.cpp
    xrSim/xrSimPackSpawn.h
    xrSim/xrSimWorldState.cpp
    xrSim/xrSimWorldState.h
)

target_include_directories(xrSimCore
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

set_target_properties(xrSimCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(XRAY_AGENT_HTTP AND CURL_FOUND)
    target_compile_definitions(xrSimCore PRIVATE XRAY_AGENT_HTTP_CURL=1)
    target_link_libraries(xrSimCore PUBLIC CURL::libcurl)
endif()

add_executable(xrSimWorldStateTests EXCLUDE_FROM_ALL
    xrSim/tests/xrSimWorldStateTests.cpp
)

target_include_directories(xrSimWorldStateTests
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(xrSimWorldStateTests
    PRIVATE
    xrSimCore
)

add_executable(xrSimAuthoritySpineTests EXCLUDE_FROM_ALL
    xrSim/tests/xrSimAuthoritySpineTests.cpp
)
target_include_directories(xrSimAuthoritySpineTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(xrSimAuthoritySpineTests PRIVATE xrSimCore)

# Configure-time source identity includes the telemetry implementation bytes and
# honestly labels the worktree; it does not claim authenticated runtime assets.
set(frame_time_identity "${XRAY_GIT_SHA}")
foreach(source FrameTimeCollector.h FrameTimeCollector.cpp FrameTimeSession.h FrameTimeSession.cpp FrameTimeClassification.h
    device.h device.cpp x_ray.cpp AgentBridge.cpp CMakeLists.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" source_hash)
    string(APPEND frame_time_identity "${source}:${source_hash};")
endforeach()
foreach(source Layers/xrRenderGL/glHW.cpp Layers/xrRender/r__sync_point.cpp Layers/xrRender/QueryHelper.h)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/${source}")
    file(SHA256 "${CMAKE_SOURCE_DIR}/src/${source}" source_hash)
    string(APPEND frame_time_identity "${source}:${source_hash};")
endforeach()
string(SHA256 frame_time_hash "${frame_time_identity}")
target_compile_definitions(xrEngine PRIVATE
    XR_FRAME_TIME_SOURCE_BUILD="git:${XRAY_GIT_SHA},telemetry_sha256:${frame_time_hash},config:$<CONFIG>"
    XR_FRAME_TIME_ALLOCATOR="${MEMORY_ALLOCATOR},$<CONFIG>")
target_sources(xrEngine PRIVATE FrameTimeCollector.cpp FrameTimeCollector.h FrameTimeSession.cpp FrameTimeSession.h)

if(NOT WIN32)
    add_executable(xrFrameTimeSessionTests EXCLUDE_FROM_ALL
        FrameTimeCollector.cpp FrameTimeSession.cpp tests/FrameTimeSessionTests.cpp)
    target_include_directories(xrFrameTimeSessionTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    set_target_properties(xrFrameTimeSessionTests PROPERTIES UNITY_BUILD OFF)
endif()

add_executable(xrFrameTimeCollectorTests EXCLUDE_FROM_ALL
    FrameTimeCollector.cpp
    tests/FrameTimeCollectorTests.cpp
)
target_include_directories(xrFrameTimeCollectorTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

add_executable(xrFrameTimeCollectorBoundaryTests EXCLUDE_FROM_ALL
    FrameTimeCollector.cpp
    tests/FrameTimeCollectorBoundaryTests.cpp
)
set_target_properties(xrFrameTimeCollectorBoundaryTests PROPERTIES UNITY_BUILD OFF)
target_include_directories(xrFrameTimeCollectorBoundaryTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)
if(NOT MSVC)
    target_compile_options(xrFrameTimeCollectorTests PRIVATE -fno-exceptions)
    target_compile_options(xrFrameTimeCollectorBoundaryTests PRIVATE -fno-exceptions)
    if(TARGET xrFrameTimeSessionTests)
        target_compile_options(xrFrameTimeSessionTests PRIVATE -fno-exceptions)
    endif()
endif()


target_sources_grouped(
    TARGET xrEngine
    NAME "editor"
    FILES
    editor_base.cpp
    editor_base.h
    editor_base_input.cpp
    editor_helper.cpp
    editor_helper.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine"
    FILES
    Properties.h
    pure.cpp
    pure.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Console"
    FILES
    XR_IOConsole.cpp
    XR_IOConsole.h
    XR_IOConsole_callback.cpp
    XR_IOConsole_control.cpp
    XR_IOConsole_get.cpp
    XR_IOConsole_script.cpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Console\\commands"
    FILES
    xr_ioc_cmd.cpp
    xr_ioc_cmd.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Core"
    FILES
    Engine.cpp
    Engine.h
    EngineAPI.cpp
    EngineAPI.h
    EventAPI.cpp
    EventAPI.h
    mailSlot.cpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Noise"
    FILES
    perlin.cpp
    perlin.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\text_editor\\edit_actions"
    FILES
    edit_actions.cpp
    edit_actions.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\text_editor\\line_editor_control"
    FILES
    line_edit_control.cpp
    line_edit_control.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Animator"
    FILES
    ObjectAnimator.cpp
    ObjectAnimator.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Cameras"
    FILES
    CameraBase.cpp
    CameraBase.h
    CameraDefs.h
    CameraManager.cpp
    CameraManager.h
    Effector.cpp
    Effector.h
    EffectorPP.cpp
    EffectorPP.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Collision"
    FILES
    cf_dynamic_mesh.cpp
    cf_dynamic_mesh.h
    xr_collide_form.cpp
    xr_collide_form.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Debug"
    FILES
    ObjectDump.cpp
    ObjectDump.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Demo"
    FILES
    FDemoPlay.cpp
    FDemoPlay.h
    FDemoRecord.cpp
    FDemoRecord.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Environment"
    FILES
    Environment.cpp
    Environment.h
    Environment_editor.cpp
    Environment_misc.cpp
    Environment_render.cpp
    xrHemisphere.cpp
    xrHemisphere.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Environment\\Effects"
    FILES
    Rain.cpp
    Rain.h
    thunderbolt.cpp
    thunderbolt.h
    xr_efflensflare.cpp
    xr_efflensflare.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Feelers"
    FILES
    Feel_Sound.h
    Feel_Touch.cpp
    Feel_Touch.h
    Feel_Vision.cpp
    Feel_Vision.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\HUD"
    FILES
    CustomHUD.cpp
    CustomHUD.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Level"
    FILES
    IGame_Level.cpp
    IGame_Level.h
    IGame_Level_check_textures.cpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Level Controller"
    FILES
    xr_level_controller.cpp
    xr_level_controller.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Level Controller\\KeyBindings"
    FILES
    key_binding_registrator_script.cpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\Objects"
    FILES
    IGame_ObjectPool.cpp
    IGame_ObjectPool.h
    IGame_Persistent.cpp
    IGame_Persistent.h
    pure_relcase.cpp
    pure_relcase.h
    xr_object.h
    xr_object_list.cpp
    xr_object_list.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Engine\\Game API\\StringTable"
    FILES
    StringTable/StringTable.cpp
    StringTable/StringTable.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "General"
    FILES
    AccessibilityShortcuts.hpp
    defines.cpp
    defines.h
    embedded_resources_management.h
    mp_logging.h
    stdafx.cpp
    stdafx.h
    x_ray.cpp
    x_ray.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "General\\Profiler"
    FILES
    profiler.cpp
    profiler.h
    profiler_inline.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\Collidable"
    FILES
    ICollidable.cpp
    ICollidable.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\ILoadingScreen"
    FILES
    ILoadingScreen.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\Input"
    FILES
    IInputReceiver.cpp
    IInputReceiver.h
    xr_input.cpp
    xr_input.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\Physics"
    FILES
    IObjectPhysicsCollision.h
    IPHdebug.h
    IPhysicsGeometry.h
    IPhysicsShell.h
    phdebug.cpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\Render"
    FILES
    IRenderable.cpp
    IRenderable.h
    Render.cpp
    Render.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Interfaces\\Sheduler"
    FILES
    ISheduled.cpp
    ISheduled.h
    xrSheduler.cpp
    xrSheduler.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Execution & 3D"
    FILES
    AgentBridge.cpp
    AgentBridge.h
    device.cpp
    device.h
    Device_create.cpp
    Device_destroy.cpp
    Device_imgui.cpp
    Device_Initialize.cpp
    Device_mode.cpp
    Device_overdraw.cpp
    IPerformanceAlert.hpp
    PerformanceAlert.cpp
    PerformanceAlert.hpp
    StatGraph.cpp
    StatGraph.h
    Stats.cpp
    Stats.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Execution & 3D\\Shaders"
    FILES
    WaveForm.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Execution & 3D\\Shaders\\Textures"
    FILES
    #tntQAVI.cpp
    #tntQAVI.h
    xrImage_Resampler.cpp
    xrImage_Resampler.h
    xrTheora_Stream.cpp
    xrTheora_Stream.h
    xrTheora_Surface.cpp
    xrTheora_Surface.h
)

#target_sources_grouped(
#    TARGET xrEngine
#    NAME "Render\\Execution & 3D\\TextConsole"
#    FILES
#    Text_Console.cpp
#    Text_Console.h
#    Text_Console_WndProc.cpp
#)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Font output"
    FILES
    GameFont.cpp
    GameFont.h
    IGameFont.hpp
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Lighting"
    FILES
    LightAnimLibrary.cpp
    LightAnimLibrary.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Particle system"
    FILES
    PS_instance.cpp
    PS_instance.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Visibility"
    FILES
    ShadersExternalData.h
    vis_common.h
    vis_object_data.h
)

target_sources_grouped(
    TARGET xrEngine
    NAME "Render\\Visuals\\Skeleton"
    FILES
    EnnumerateVertices.h
)

# XXX: This is only used in utils, should be moved somewhere to utils
#target_sources_grouped(
#    TARGET xrEngine
#    NAME ""
#    FILES
#    xrLoadSurface.cpp
#)

target_sources(xrEngine
    PRIVATE
    ClientServer.txt
    "Effects description.txt"
    features.txt
    TODO.txt
)

target_include_directories(xrEngine
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/Externals/imgui"
    "${CMAKE_SOURCE_DIR}/Externals/OpenAutomate/inc"
)

target_link_libraries(xrEngine
    PUBLIC
    xrMiscMath

    PRIVATE
    xrSimCore
    xrAPI
    xrCDB
    xrMaterialSystem
    xrCore
    xrSound
    xrScriptEngine
    xrNetServer
    xrImGui
    OpenAL::OpenAL
    Ogg::Ogg
    Theora::Theora
    SDL2::SDL2
)

if (APPLE)
    # Native window occlusion classification before loading/not-ready returns.
    target_link_libraries(xrEngine PRIVATE objc)
endif()

target_compile_definitions(xrEngine
    PRIVATE
    ENGINE_BUILD
    PURE_DYNAMIC_CAST
)

set_target_properties(xrEngine PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrEngine
    PRIVATE
    stdafx.h
)

if (BUILD_SHARED_LIBS)
    install(TARGETS xrEngine LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
