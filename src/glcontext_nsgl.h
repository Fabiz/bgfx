/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef BGFX_GLCONTEXT_NSGL_H_HEADER_GUARD
#define BGFX_GLCONTEXT_NSGL_H_HEADER_GUARD

#if BX_PLATFORM_OSX

namespace bgfx { namespace gl
{
struct SwapChainGL;

struct GlContext
{
    GlContext()
            : m_context(NULL)
            , m_view(NULL)
            , m_msaaContext(false)
    {
    }

    void create(uint32_t _width, uint32_t _height, uint32_t _flags);
    void destroy();
    void resize(uint32_t _width, uint32_t _height, uint32_t _flags);

    uint64_t getCaps() const;
    SwapChainGL* createSwapChain(void* _nwh);
    void destroySwapChain(SwapChainGL*  _swapChain);
    void swap(SwapChainGL* _swapChain = NULL);
    void makeCurrent(SwapChainGL* _swapChain = NULL);

    void import();

    bool isValid() const
    {
        return NULL != m_context;
    }

    void* m_context;
    void* m_view;
    // true when MSAA is handled by the context instead of using MSAA FBO
    bool m_msaaContext;

};
} /* namespace gl */ } // namespace bgfx

#endif // BX_PLATFORM_OSX

#endif // BGFX_GLCONTEXT_NSGL_H_HEADER_GUARD