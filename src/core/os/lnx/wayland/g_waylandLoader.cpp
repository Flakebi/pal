/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2018-2019 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!
//
// This code has been generated automatically. Do not hand-modify this code.
//
// Modify the procsAnalysis.py and waylandLoader.py in the tools/generate directory OR waylandWindowSystem.proc instead
//
// WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING!  WARNING! WARNING!  WARNING!  WARNING!  WARNING!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <dlfcn.h>
#include <string.h>
#include "core/os/lnx/wayland/g_waylandLoader.h"
#include "palAssert.h"

#include "palSysUtil.h"

using namespace Util;
namespace Pal
{
namespace Linux
{

// =====================================================================================================================
#if defined(PAL_DEBUG_PRINTS)
void WaylandLoaderFuncsProxy::Init(const char* pLogPath)
{
    char file[128] = {0};
    Util::Snprintf(file, sizeof(file), "%s/WaylandLoaderTimeLogger.csv", pLogPath);
    m_timeLogger.Open(file, FileAccessMode::FileAccessWrite);
    Util::Snprintf(file, sizeof(file), "%s/WaylandLoaderParamLogger.trace", pLogPath);
    m_paramLogger.Open(file, FileAccessMode::FileAccessWrite);
}

// =====================================================================================================================

// =====================================================================================================================
wl_event_queue* WaylandLoaderFuncsProxy::pfnWlDisplayCreateQueue(
    struct wl_display*  display
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    wl_event_queue* pRet = m_pFuncs->pfnWlDisplayCreateQueue(display);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlDisplayCreateQueue,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlDisplayCreateQueue(%p)\n",
        display);
    m_paramLogger.Flush();

    return pRet;
}

// =====================================================================================================================
int WaylandLoaderFuncsProxy::pfnWlDisplayDispatchQueue(
    struct wl_display*      display,
    struct wl_event_queue*  queue
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    int ret = m_pFuncs->pfnWlDisplayDispatchQueue(display,
                                                  queue);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlDisplayDispatchQueue,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlDisplayDispatchQueue(%p, %p)\n",
        display,
        queue);
    m_paramLogger.Flush();

    return ret;
}

// =====================================================================================================================
int WaylandLoaderFuncsProxy::pfnWlDisplayDispatchQueuePending(
    struct wl_display*      display,
    struct wl_event_queue*  queue
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    int ret = m_pFuncs->pfnWlDisplayDispatchQueuePending(display,
                                                         queue);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlDisplayDispatchQueuePending,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlDisplayDispatchQueuePending(%p, %p)\n",
        display,
        queue);
    m_paramLogger.Flush();

    return ret;
}

// =====================================================================================================================
int WaylandLoaderFuncsProxy::pfnWlDisplayFlush(
    struct wl_display*  display
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    int ret = m_pFuncs->pfnWlDisplayFlush(display);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlDisplayFlush,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlDisplayFlush(%p)\n",
        display);
    m_paramLogger.Flush();

    return ret;
}

// =====================================================================================================================
int WaylandLoaderFuncsProxy::pfnWlDisplayRoundtripQueue(
    struct wl_display*      display,
    struct wl_event_queue*  queue
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    int ret = m_pFuncs->pfnWlDisplayRoundtripQueue(display,
                                                   queue);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlDisplayRoundtripQueue,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlDisplayRoundtripQueue(%p, %p)\n",
        display,
        queue);
    m_paramLogger.Flush();

    return ret;
}

// =====================================================================================================================
void WaylandLoaderFuncsProxy::pfnWlEventQueueDestroy(
    struct wl_event_queue*  queue
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    m_pFuncs->pfnWlEventQueueDestroy(queue);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlEventQueueDestroy,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlEventQueueDestroy(%p)\n",
        queue);
    m_paramLogger.Flush();
}

// =====================================================================================================================
int WaylandLoaderFuncsProxy::pfnWlProxyAddListener(
    struct wl_proxy*  proxy,
    void              (**implementation)(void),
    void*             data
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    int ret = m_pFuncs->pfnWlProxyAddListener(proxy,
                                              (**implementation)(void),
                                              data);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyAddListener,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyAddListener(%p, %x, %p)\n",
        proxy,
        (**implementation)(void),
        data);
    m_paramLogger.Flush();

    return ret;
}

// =====================================================================================================================
void* WaylandLoaderFuncsProxy::pfnWlProxyCreateWrapper(
    void*  proxy
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    void* pRet = m_pFuncs->pfnWlProxyCreateWrapper(proxy);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyCreateWrapper,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyCreateWrapper(%p)\n",
        proxy);
    m_paramLogger.Flush();

    return pRet;
}

// =====================================================================================================================
void WaylandLoaderFuncsProxy::pfnWlProxyDestroy(
    struct wl_proxy*  proxy
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    m_pFuncs->pfnWlProxyDestroy(proxy);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyDestroy,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyDestroy(%p)\n",
        proxy);
    m_paramLogger.Flush();
}

// =====================================================================================================================
void WaylandLoaderFuncsProxy::pfnWlProxyMarshal(
    struct wl_proxy*  p,
    uint32_t          opcode,
                      ...
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    m_pFuncs->pfnWlProxyMarshal(p,
                                opcode,
                                ...);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyMarshal,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyMarshal(%p, %x, %x)\n",
        p,
        opcode,
        ...);
    m_paramLogger.Flush();
}

// =====================================================================================================================
wl_proxy* WaylandLoaderFuncsProxy::pfnWlProxyMarshalConstructor(
    struct wl_proxy*            proxy,
    uint32_t                    opcode,
    const struct wl_interface*  interface,
                                ...
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    wl_proxy* pRet = m_pFuncs->pfnWlProxyMarshalConstructor(proxy,
                                                            opcode,
                                                            interface,
                                                            ...);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyMarshalConstructor,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyMarshalConstructor(%p, %x, %p, %x)\n",
        proxy,
        opcode,
        interface,
        ...);
    m_paramLogger.Flush();

    return pRet;
}

// =====================================================================================================================
wl_proxy* WaylandLoaderFuncsProxy::pfnWlProxyMarshalConstructorVersioned(
    struct wl_proxy*            proxy,
    uint32_t                    opcode,
    const struct wl_interface*  interface,
    uint32_t                    version,
                                ...
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    wl_proxy* pRet = m_pFuncs->pfnWlProxyMarshalConstructorVersioned(proxy,
                                                                     opcode,
                                                                     interface,
                                                                     version,
                                                                     ...);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyMarshalConstructorVersioned,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyMarshalConstructorVersioned(%p, %x, %p, %x, %x)\n",
        proxy,
        opcode,
        interface,
        version,
        ...);
    m_paramLogger.Flush();

    return pRet;
}

// =====================================================================================================================
void WaylandLoaderFuncsProxy::pfnWlProxySetQueue(
    struct wl_proxy*        proxy,
    struct wl_event_queue*  queue
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    m_pFuncs->pfnWlProxySetQueue(proxy,
                                 queue);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxySetQueue,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxySetQueue(%p, %p)\n",
        proxy,
        queue);
    m_paramLogger.Flush();
}

// =====================================================================================================================
void WaylandLoaderFuncsProxy::pfnWlProxyWrapperDestroy(
    void*  proxy_wrapper
    ) const
{
    const int64 begin = Util::GetPerfCpuTime();
    m_pFuncs->pfnWlProxyWrapperDestroy(proxy_wrapper);
    const int64 end = Util::GetPerfCpuTime();
    const int64 elapse = end - begin;
    m_timeLogger.Printf("WlProxyWrapperDestroy,%ld,%ld,%ld\n", begin, end, elapse);
    m_timeLogger.Flush();

    m_paramLogger.Printf(
        "WlProxyWrapperDestroy(%p)\n",
        proxy_wrapper);
    m_paramLogger.Flush();
}

#endif

// =====================================================================================================================
WaylandLoader::WaylandLoader()
    :
    m_pWlRegistryInterface(nullptr),
    m_pWlBufferInterface(nullptr),
    m_pWlCallbackInterface(nullptr),
    m_initialized(false)
{
    memset(m_libraryHandles, 0, sizeof(m_libraryHandles));
    memset(&m_funcs, 0, sizeof(m_funcs));
}

// =====================================================================================================================
wl_interface* WaylandLoader::GetWlRegistryInterface() const
{
    return m_pWlRegistryInterface;
}

// =====================================================================================================================
wl_interface* WaylandLoader::GetWlBufferInterface() const
{
    return m_pWlBufferInterface;
}

// =====================================================================================================================
wl_interface* WaylandLoader::GetWlCallbackInterface() const
{
    return m_pWlCallbackInterface;
}

// =====================================================================================================================
WaylandLoader::~WaylandLoader()
{
    if (m_libraryHandles[LibWaylandClient] != nullptr)
    {
        dlclose(m_libraryHandles[LibWaylandClient]);
    }
}

// =====================================================================================================================
Result WaylandLoader::Init(
    Platform* pPlatform)
{
    Result result                   = Result::Success;
    constexpr uint32_t LibNameSize  = 64;
    char LibNames[WaylandLoaderLibrariesCount][LibNameSize] = {
        "libwayland-client.so.0",
    };

    if (m_initialized == false)
    {
        // resolve symbols from libwayland-client.so.0
        m_libraryHandles[LibWaylandClient] = dlopen(LibNames[LibWaylandClient], RTLD_LAZY);
        if (m_libraryHandles[LibWaylandClient] == nullptr)
        {
            result = Result::ErrorUnavailable;
        }
        else
        {
            m_funcs.pfnWlDisplayCreateQueue = reinterpret_cast<WlDisplayCreateQueue>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_display_create_queue"));
            m_funcs.pfnWlDisplayDispatchQueue = reinterpret_cast<WlDisplayDispatchQueue>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_display_dispatch_queue"));
            m_funcs.pfnWlDisplayDispatchQueuePending = reinterpret_cast<WlDisplayDispatchQueuePending>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_display_dispatch_queue_pending"));
            m_funcs.pfnWlDisplayFlush = reinterpret_cast<WlDisplayFlush>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_display_flush"));
            m_funcs.pfnWlDisplayRoundtripQueue = reinterpret_cast<WlDisplayRoundtripQueue>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_display_roundtrip_queue"));
            m_funcs.pfnWlEventQueueDestroy = reinterpret_cast<WlEventQueueDestroy>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_event_queue_destroy"));
            m_funcs.pfnWlProxyAddListener = reinterpret_cast<WlProxyAddListener>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_add_listener"));
            m_funcs.pfnWlProxyCreateWrapper = reinterpret_cast<WlProxyCreateWrapper>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_create_wrapper"));
            m_funcs.pfnWlProxyDestroy = reinterpret_cast<WlProxyDestroy>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_destroy"));
            m_funcs.pfnWlProxyMarshal = reinterpret_cast<WlProxyMarshal>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_marshal"));
            m_funcs.pfnWlProxyMarshalConstructor = reinterpret_cast<WlProxyMarshalConstructor>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_marshal_constructor"));
            m_funcs.pfnWlProxyMarshalConstructorVersioned = reinterpret_cast<WlProxyMarshalConstructorVersioned>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_marshal_constructor_versioned"));
            m_funcs.pfnWlProxySetQueue = reinterpret_cast<WlProxySetQueue>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_set_queue"));
            m_funcs.pfnWlProxyWrapperDestroy = reinterpret_cast<WlProxyWrapperDestroy>(dlsym(
                        m_libraryHandles[LibWaylandClient],
                        "wl_proxy_wrapper_destroy"));
        }

        if (m_libraryHandles[LibWaylandClient] == nullptr)
        {
            result = Result::ErrorUnavailable;
        }
        else
        {
            m_pWlRegistryInterface = reinterpret_cast<wl_interface*>(dlsym(m_libraryHandles[LibWaylandClient], "wl_registry_interface"));
        }
        if (m_libraryHandles[LibWaylandClient] == nullptr)
        {
            result = Result::ErrorUnavailable;
        }
        else
        {
            m_pWlBufferInterface = reinterpret_cast<wl_interface*>(dlsym(m_libraryHandles[LibWaylandClient], "wl_buffer_interface"));
        }
        if (m_libraryHandles[LibWaylandClient] == nullptr)
        {
            result = Result::ErrorUnavailable;
        }
        else
        {
            m_pWlCallbackInterface = reinterpret_cast<wl_interface*>(dlsym(m_libraryHandles[LibWaylandClient], "wl_callback_interface"));
        }
        if (result == Result::Success)
        {
            m_initialized = true;
#if defined(PAL_DEBUG_PRINTS)
            m_proxy.SetFuncCalls(&m_funcs);
#endif
        }
    }
    return result;
}

} //namespace Linux
} //namespace Pal
