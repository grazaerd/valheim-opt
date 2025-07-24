#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include <basetsd.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <minwindef.h>
#include <winnt.h>
#include <immintrin.h>

#include "impl.h"
#include "MinHook.h"
#include "shaderbool.h"

#include "util.h"
#include "shaders/snow.hpp"


namespace atfix {

/** Hooking-related stuff */
using PFN_ID3D11Device_CreateVertexShader = HRESULT(STDMETHODCALLTYPE*) (ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
using PFN_ID3D11Device_CreatePixelShader = HRESULT(STDMETHODCALLTYPE*) (ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
using PFN_ID3D11Device_CreateBuffer = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);

struct DeviceProcs {
    PFN_ID3D11Device_CreateBuffer CreateBuffer = nullptr;
    PFN_ID3D11Device_CreateVertexShader CreateVertexShader = nullptr;
    PFN_ID3D11Device_CreatePixelShader CreatePixelShader = nullptr;
};

using PFN_ID3D11DeviceContext_IASetIndexBuffer = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, DXGI_FORMAT, UINT);
using PFN_ID3D11DeviceContext_Map = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using PFN_ID3D11DeviceContext_DrawIndexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using PFN_ID3D11DeviceContext_PSSetShader = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*,ID3D11ClassInstance* const*, UINT);
struct ContextProcs {
    PFN_ID3D11DeviceContext_Map Map = nullptr;
    PFN_ID3D11DeviceContext_IASetIndexBuffer IASetIndexBuffer = nullptr;
    PFN_ID3D11DeviceContext_DrawIndexed DrawIndexed = nullptr;
    PFN_ID3D11DeviceContext_PSSetShader                     PSSetShader                     = nullptr;
};

namespace {
    mutex  g_hookMutex;
    mutex  g_hookMutex2;
    uint32_t g_installedHooks = 0U;
}

inline bool simd_equal(const std::array<uint32_t, 4>& arr1, const uint32_t* ptr) {
    const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(arr1.data()));
    const __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));

    return (_mm_movemask_epi8(_mm_cmpeq_epi32(v1, v2)) == 0xFFFF);
}
DeviceProcs   g_deviceProcs;
ContextProcs  g_immContextProcs;
ContextProcs  g_defContextProcs;

constexpr uint32_t HOOK_DEVICE  = (1u << 0);
constexpr uint32_t HOOK_IMM_CTX = (1u << 1);
constexpr uint32_t HOOK_DEF_CTX = (1u << 2);

inline const DeviceProcs* getDeviceProcs([[maybe_unused]] ID3D11Device* pDevice) {
    return &g_deviceProcs;
}
inline const ContextProcs* getContextProcs(ID3D11DeviceContext* pContext) {
    return pContext->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE
        ? &g_immContextProcs
        : &g_defContextProcs;
}

inline bool isImmediatecontext(
        ID3D11DeviceContext*      pContext) {
  return pContext->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
}
std::unordered_map<ID3D11PixelShader*, std::vector<uint8_t>> g_PixelShaderBlobs;
HRESULT STDMETHODCALLTYPE ID3D11Device_CreateVertexShader(
        ID3D11Device*           pDevice,
        const void*             pShaderBytecode,
        SIZE_T                  BytecodeLength,
        ID3D11ClassLinkage*     pClassLinkage,
        ID3D11VertexShader**    ppVertexShader) {
    const auto* procs = getDeviceProcs(pDevice);

    return procs->CreateVertexShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
}

ID3D11PixelShader* DefPS = nullptr;
ID3D11PixelShader* OriginalDefPS = nullptr;
ID3D11VertexShader* DefVS = nullptr;

HRESULT STDMETHODCALLTYPE ID3D11Device_CreatePixelShader(
    ID3D11Device* pDevice,
    const void* pShaderBytecode,
    SIZE_T                  BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11PixelShader** ppPixelShader) {
    const auto* procs = getDeviceProcs(pDevice);
	
	if (!DefPS) {
		procs->CreatePixelShader(pDevice, data.data(), data.size(), pClassLinkage, &DefPS);
		*ppPixelShader = DefPS;
		DefPS->AddRef();
        return procs->CreatePixelShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
	}
	if (!OriginalDefPS) {
		procs->CreatePixelShader(pDevice, original.data(), original.size(), pClassLinkage, &OriginalDefPS);
		*ppPixelShader = OriginalDefPS;
		OriginalDefPS->AddRef();
        return procs->CreatePixelShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
	}
    return procs->CreatePixelShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
}

void STDMETHODCALLTYPE ID3D11DeviceContext_DrawIndexed(
        ID3D11DeviceContext* pContext,
        UINT IndexCount,
        UINT StartIndexLocation,
        INT BaseVertexLocation) {
    const auto* procs = getContextProcs(pContext);
    if (IndexCount == 9000 || IndexCount == 3942) {                
        pContext->PSSetShader(DefPS, nullptr, 0);
        procs->DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
        pContext->PSSetShader(nullptr, nullptr, 0);
        return;
    }

    procs->DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
}


#define HOOK_PROC(iface, object, table, index, proc) \
  hookProc(object, #iface "::" #proc, &table->proc, &iface ## _ ## proc, index)


template<typename T>
void hookProc(void* pObject,[[maybe_unused]] const char* pName, T** ppOrig, T* pHook, uint32_t index) {
    void** vtbl = *std::bit_cast<void***>(pObject);

    MH_STATUS mh = MH_CreateHook(vtbl[index], std::bit_cast<void*>(pHook), std::bit_cast<void**>(ppOrig));

    if (mh) {
        if (mh != MH_ERROR_ALREADY_CREATED) {
#ifndef NDEBUG
            log("Failed to create hook for ", pName, ": ", MH_StatusToString(mh));
#endif
        }
        return;
    }

    mh = MH_EnableHook(vtbl[index]);

    if (mh) {
#ifndef NDEBUG
        log("Failed to enable hook for ", pName, ": ", MH_StatusToString(mh));
#endif
        return;
    }
#ifndef NDEBUG
        log("Created hook for ", pName, " @ ", reinterpret_cast<void*>(pHook));
    #endif
}

void hookDevice(ID3D11Device* pDevice) {
    const std::lock_guard lock(g_hookMutex);

    if (g_installedHooks & HOOK_DEVICE) {
        return;
    }

#ifndef NDEBUG
    log("Hooking device ", pDevice);
#endif

    DeviceProcs* procs = &g_deviceProcs;
    // HOOK_PROC(ID3D11Device, pDevice, procs, 12,  CreateVertexShader);
    HOOK_PROC(ID3D11Device, pDevice, procs, 15,  CreatePixelShader);

    g_installedHooks |= HOOK_DEVICE;
}
void hookContext(ID3D11DeviceContext* pContext) {
  std::lock_guard lock(g_hookMutex);

  uint32_t flag = HOOK_IMM_CTX;
  ContextProcs* procs = &g_immContextProcs;

  if (!isImmediatecontext(pContext)) {
    flag = HOOK_DEF_CTX;
    procs = &g_defContextProcs;
  }

  if (g_installedHooks & flag)
    return;

   HOOK_PROC(ID3D11DeviceContext, pContext, procs, 12, DrawIndexed);

  g_installedHooks |= flag;

  /* Immediate context and deferred context methods may share code */
  if (flag & HOOK_IMM_CTX)
    g_defContextProcs = g_immContextProcs;
}
}