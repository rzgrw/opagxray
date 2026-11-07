#pragma once

#if defined(USE_METAL)
#include "Layers/xrRenderMetal/metalOcclusionQuery.h"
#include "Layers/xrRenderMetal/metalRenderPassManager.h"
#endif

namespace xray::render::RENDER_NAMESPACE
{
//	Interface
#if defined(USE_DX11)
IC HRESULT CreateQuery(ID3DQuery** ppQuery);
IC HRESULT GetData(ID3DQuery* pQuery, void* pData, u32 DataSize);
IC HRESULT BeginQuery(ID3DQuery* pQuery);
IC HRESULT EndQuery(ID3DQuery* pQuery);
IC HRESULT ReleaseQuery(ID3DQuery *pQuery);
#elif defined(USE_OGL)
IC HRESULT CreateQuery(GLuint* pQuery, D3D_QUERY type);
IC HRESULT GetData(GLuint query, void* pData, u32 DataSize);
IC HRESULT BeginQuery(GLuint query);
IC HRESULT EndQuery(GLuint query);
IC HRESULT ReleaseQuery(GLuint pQuery);
#elif defined(USE_METAL)
IC HRESULT CreateQuery(uint64_t* pQuery, D3D_QUERY type);
IC HRESULT GetData(uint64_t query, void* pData, u32 DataSize);
IC HRESULT BeginQuery(uint64_t query);
IC HRESULT EndQuery(uint64_t query);
IC HRESULT ReleaseQuery(uint64_t pQuery);
#else
#   error No graphics API selected or enabled!
#endif

//	Implementation

#if defined(USE_DX11)

IC HRESULT CreateQuery(ID3DQuery** ppQuery, D3D_QUERY type)
{
    D3D_QUERY_DESC desc;
    desc.MiscFlags = 0;
    desc.Query = type;
    return HW.pDevice->CreateQuery(&desc, ppQuery);
}

IC HRESULT GetData(ID3DQuery* pQuery, void* pData, u32 DataSize)
{
    //	Use D3Dxx_ASYNC_GETDATA_DONOTFLUSH for prevent flushing
    return HW.get_context(CHW::IMM_CTX_ID)->GetData(pQuery, pData, DataSize, 0); // we can fetch data on imm only
}

IC HRESULT BeginQuery(ID3DQuery* pQuery)
{
    HW.get_context(CHW::IMM_CTX_ID)->Begin(pQuery);
    return S_OK;
}

IC HRESULT EndQuery(ID3DQuery* pQuery)
{
    HW.get_context(CHW::IMM_CTX_ID)->End(pQuery);
    return S_OK;
}

IC HRESULT ReleaseQuery(ID3DQuery* pQuery)
{
    _RELEASE(pQuery);
    return S_OK;
}

#elif defined(USE_OGL)

IC HRESULT CreateQuery(GLuint* pQuery, D3D_QUERY type)
{
    R_ASSERT(type == D3D_QUERY_OCCLUSION);
    glGenQueries(1, pQuery);
    return S_OK;
}

IC HRESULT GetData(GLuint query, void* pData, u32 DataSize)
{
    xray::frametime::FrameTimeScope span(Device.FrameTime, xray::frametime::FrameTimePhase::OcclusionWait);
    if (DataSize == sizeof(GLint64))
        CHK_GL(glGetQueryObjecti64v(query, GL_QUERY_RESULT, (GLint64*)pData));
    else
        CHK_GL(glGetQueryObjectiv(query, GL_QUERY_RESULT, (GLint*)pData));
    return S_OK;
}

IC HRESULT BeginQuery(GLuint query)
{
    CHK_GL(glBeginQuery(GL_SAMPLES_PASSED, query));
    return S_OK;
}

IC HRESULT EndQuery(GLuint query)
{
    CHK_GL(glEndQuery(GL_SAMPLES_PASSED));
    return S_OK;
}

IC HRESULT ReleaseQuery(GLuint query)
{
    CHK_GL(glDeleteQueries(1, &query));
    return S_OK;
}

#elif defined(USE_METAL)

// Queries are slots in the shared MTLVisibilityResultBuffer owned by
// metalOcclusionQuery (see Layers/xrRenderMetal/metalOcclusionQuery.h).
// Handle encoding: slot index + 1; 0 means "invalid handle".

// Visibility buffer slot count (u64 each). Must cover R_occlusion's demand
// (occq_size in r__occlusion.h); allocation fails gracefully past the limit.
constexpr u32 metal_occq_pool_size = 4096;

IC HRESULT CreateQuery(uint64_t* pQuery, D3D_QUERY type)
{
    R_ASSERT(type == D3D_QUERY_OCCLUSION);
    if (!OcclusionQueries.Created())
        OcclusionQueries.Create(metal_occq_pool_size);
    const u32 index = OcclusionQueries.Allocate();
    if (index == metalOcclusionQuery::InvalidIndex)
    {
        *pQuery = 0;
        return E_FAIL;
    }
    *pQuery = uint64_t(index) + 1;
    return S_OK;
}

IC HRESULT GetData(uint64_t query, void* pData, u32 DataSize)
{
    if (!query)
    {
        ZeroMemory(pData, DataSize);
        return S_OK;
    }
    const u64 result = OcclusionQueries.GetResult(u32(query - 1));
    if (DataSize >= sizeof(u64))
        *static_cast<u64*>(pData) = result;
    else
        *static_cast<u32*>(pData) = result > u64(type_max<u32>) ? type_max<u32> : u32(result);
    return S_OK;
}

IC HRESULT BeginQuery(uint64_t query)
{
    if (!query)
        return E_FAIL;
    // Never begins a pass: outside an open pass the query counts nothing (0),
    // matching "occluded" — R_occlusion re-issues queries every frame.
    OcclusionQueries.Begin(u32(query - 1), metalRenderPass::ActiveEncoder());
    return S_OK;
}

IC HRESULT EndQuery(uint64_t query)
{
    if (!query)
        return E_FAIL;
    OcclusionQueries.End(u32(query - 1), metalRenderPass::ActiveEncoder());
    return S_OK;
}

IC HRESULT ReleaseQuery(uint64_t query)
{
    if (query)
        OcclusionQueries.Release(u32(query - 1));
    return S_OK;
}

#else
#   error No graphics API selected or enabled!
#endif
} // namespace xray::render::RENDER_NAMESPACE
