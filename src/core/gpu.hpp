#ifndef CORE_GPU_HPP
#define CORE_GPU_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <common/bit.hpp>
#include <common/bitfield.hpp>

namespace Core
{

class Gpu {
public:
    Gpu();

    void Reset();

    inline const void * Framebuffer() const { return m_vram.data(); };

    uint32_t GpuRead();
    uint32_t GpuStat();

    void Gp0(uint32_t data);
    void Gp1(uint32_t data);

private:
    void ExecuteCommand();

    void UpdateGpustat();

    inline uint16_t ReadVram(size_t x, size_t y) const
    {
        assert(x < VramWidth);
        assert(y < VramHeight);

        return m_vram[VramWidth * y + x];
    }

    inline void WriteVram(size_t x, size_t y, uint16_t data)
    {
        assert(x < VramWidth);
        assert(y < VramHeight);

        m_vram[VramWidth * y + x] = data;
    }

    struct Clut {
        size_t x, y;
    };

    union Color32 {
        uint32_t raw;

        BitField<uint32_t, uint8_t, 0, 8> r;
        BitField<uint32_t, uint8_t, 8, 8> g;
        BitField<uint32_t, uint8_t, 16, 8> b;
        BitField<uint32_t, bool, 24, 1> a;
    };

    union Color16 {
        uint16_t raw;

        BitField<uint16_t, uint8_t, 0, 5> r;
        BitField<uint16_t, uint8_t, 5, 5> g;
        BitField<uint16_t, uint8_t, 10, 5> b;
        BitField<uint16_t, bool, 15, 1> a;
    };

    uint16_t FetchTexel(uint8_t u, uint8_t v, Clut clut);

    enum Polygon {
        None = 0,
        RawTexture = 0x1,
        SemiTransparent = 0x2,
        Textured = 0x4,
        Quad = 0x8,
        Shaded = 0x10
    };

    void DitherPixel(int32_t x, int32_t y, Color32& c) {
        static const int dither_table[4][4] = {
            { -4, +0, -3, +1 },
            { +2, -2, +3, -1 },
            { -3, +1, -4, +0 },
            { +3, -1, +2, -2 },
        };

        const int offset = dither_table[x & 0x3][y & 0x3];
        c.r = std::clamp(static_cast<int>(c.r) + offset, 0, 255);
        c.g = std::clamp(static_cast<int>(c.g) + offset, 0, 255);
        c.b = std::clamp(static_cast<int>(c.b) + offset, 0, 255);
    }

    void BlendPixel(int32_t x, int32_t y, Color16& f) {
        Color16 b;
        b.raw = ReadVram(x, y);

        const int fr = f.r;
        const int fg = f.g;
        const int fb = f.b;

        const int br = b.r;
        const int bg = b.g;
        const int bb = b.b;

        switch (m_texpage.semi_transparency) {
        case SemiTransparency::Average:
            f.r = std::clamp((br + fr) / 2, 0, 31);
            f.g = std::clamp((bg + fg) / 2, 0, 31);
            f.b = std::clamp((bb + fb) / 2, 0, 31);
            break;
        case SemiTransparency::Add:
            f.r = std::clamp(br + fr, 0, 31);
            f.g = std::clamp(bg + fg, 0, 31);
            f.b = std::clamp(bb + fb, 0, 31);
            break;
        case SemiTransparency::Sub:
            f.r = std::clamp(br - fr, 0, 31);
            f.g = std::clamp(bg - fg, 0, 31);
            f.b = std::clamp(bb - fb, 0, 31);
            break;
        case SemiTransparency::AddQuarter:
            f.r = std::clamp(br + fr / 4, 0, 31);
            f.g = std::clamp(bg + fg / 4, 0, 31);
            f.b = std::clamp(bb + fb / 4, 0, 31);
            break;
        }
    }

    template <size_t Settings> 
    void DrawPixel(int32_t x, int32_t y, Color32 c)
    {
        constexpr bool textured = Bit::Check<Polygon::Textured>(Settings);
        constexpr bool raw_texture = Bit::Check<Polygon::RawTexture>(Settings);
        constexpr bool shaded = Bit::Check<Polygon::Shaded>(Settings);

        if constexpr ((textured && !raw_texture) || shaded) {
            if (m_texpage.dither) {
                DitherPixel(x, y, c);
            }
        }

        Color16 d;
        d.r = c.r >> 3;
        d.g = c.g >> 3;
        d.b = c.b >> 3;
        d.a = c.a;

        if constexpr ((Settings & Polygon::SemiTransparent) != 0) {
            if (((Settings & Polygon::Textured) == 0) || c.a) {
                BlendPixel(x, y, d);
            }
        }

        WriteVram(x, y, d.raw);
    }

    struct F {
        uint32_t rgb0;
        uint32_t xy[0];
    };

    struct T {
        uint32_t rgb0;
        struct {
            uint32_t xy, uv;
        } xyuv[0];
    } __attribute__((packed));

    struct G {
        struct {
            uint32_t rgb, xy;
        } rgbxy[0];
    };

    struct GT {
        struct {
            uint32_t rgb, xy, uv;
        } rgbxyuv[0];
    };

    struct Vertex {
        int16_t x, y;
        uint8_t u, v, r, g, b;
    };

    template <size_t Settings>
    void DrawPolygon()
    {
        constexpr size_t p = Settings & 0x1c;

        struct Vertex vertices[4];
        Clut clut;

        if constexpr (p == Polygon::None) {
            const struct F *f = reinterpret_cast<F *>(m_command_fifo.data());

            for (size_t i = 0; i < 3; ++i) {
                vertices[i].x = SignExtend<11>(f->xy[i]);
                vertices[i].y = SignExtend<11>(f->xy[i] >> 16);

                vertices[i].r = f->rgb0 & 0xff;
                vertices[i].g = (f->rgb0 >> 8) & 0xff;
                vertices[i].b = (f->rgb0 >> 16) & 0xff;
            }
        }

        if constexpr (p == Polygon::Textured) {
            const struct T *t = reinterpret_cast<T *>(m_command_fifo.data());

            for (size_t i = 0; i < 3; ++i) {
                vertices[i].x = SignExtend<11>(t->xyuv[i].xy);
                vertices[i].y = SignExtend<11>(t->xyuv[i].xy >> 16);

                vertices[i].u = t->xyuv[i].uv & 0xff;
                vertices[i].v = (t->xyuv[i].uv >> 8) & 0xff;

                vertices[i].r = t->rgb0 & 0xff;
                vertices[i].g = (t->rgb0 >> 8) & 0xff;
                vertices[i].b = (t->rgb0 >> 16) & 0xff;
            }

            clut.x = (t->xyuv[0].uv >> 12) & 0x3f0;
            clut.y = (t->xyuv[0].uv >> 22) & 0x1ff;

            m_texpage.raw &= 0x3600;
            m_texpage.raw |= ((t->xyuv[1].uv >> 16) & 0x09ff);
        }

        if constexpr (p == Polygon::Shaded) {
            const struct G *g = reinterpret_cast<G *>(m_command_fifo.data());

            for (size_t i = 0; i < 3; ++i) {
                vertices[i].x = SignExtend<11>(g->rgbxy[i].xy);
                vertices[i].y = SignExtend<11>(g->rgbxy[i].xy >> 16);

                vertices[i].r = g->rgbxy[i].rgb & 0xff;
                vertices[i].g = (g->rgbxy[i].rgb >> 8) & 0xff;
                vertices[i].b = (g->rgbxy[i].rgb >> 16) & 0xff;
            }
        }

        if constexpr (p == (Polygon::Shaded | Polygon::Textured)) {
            const struct GT *gt = reinterpret_cast<GT *>(m_command_fifo.data());

            for (size_t i = 0; i < 3; ++i) {
                vertices[i].x = SignExtend<11>(gt->rgbxyuv[i].xy);
                vertices[i].y = SignExtend<11>(gt->rgbxyuv[i].xy >> 16);

                vertices[i].u = gt->rgbxyuv[i].uv & 0xff;
                vertices[i].v = (gt->rgbxyuv[i].uv >> 8) & 0xff;

                vertices[i].r = gt->rgbxyuv[i].rgb & 0xff;
                vertices[i].g = (gt->rgbxyuv[i].rgb >> 8) & 0xff;
                vertices[i].b = (gt->rgbxyuv[i].rgb >> 16) & 0xff;
            }

            clut.x = (gt->rgbxyuv[0].uv >> 12) & 0x3f0;
            clut.y = (gt->rgbxyuv[0].uv >> 22) & 0x1ff;

            m_texpage.raw &= 0x3600;
            m_texpage.raw |= ((gt->rgbxyuv[1].uv >> 16) & 0x09ff); 
        }

        if constexpr (p == Polygon::Quad) {
            const struct F *f = reinterpret_cast<F *>(m_command_fifo.data());

            for (size_t i = 0; i < 4; ++i) {
                vertices[i].x = SignExtend<11>(f->xy[i]);
                vertices[i].y = SignExtend<11>(f->xy[i] >> 16);

                vertices[i].r = f->rgb0 & 0xff;
                vertices[i].g = (f->rgb0 >> 8) & 0xff;
                vertices[i].b = (f->rgb0 >> 16) & 0xff;
            }
        }

        if constexpr (p == (Polygon::Quad | Polygon::Textured)) {
            const struct T *t = reinterpret_cast<T *>(m_command_fifo.data());

            for (size_t i = 0; i < 4; ++i) {
                vertices[i].x = SignExtend<11>(t->xyuv[i].xy);
                vertices[i].y = SignExtend<11>(t->xyuv[i].xy >> 16);

                vertices[i].u = t->xyuv[i].uv;
                vertices[i].v = t->xyuv[i].uv >> 8;

                vertices[i].r = t->rgb0;
                vertices[i].g = t->rgb0 >> 8;
                vertices[i].b = t->rgb0 >> 16;
            }

            clut.x = (t->xyuv[0].uv >> 12) & 0x3f0;
            clut.y = (t->xyuv[0].uv >> 22) & 0x1ff;

            m_texpage.raw &= 0x3600;
            m_texpage.raw |= ((t->xyuv[1].uv >> 16) & 0x09ff); 
        }

        if constexpr (p == (Polygon::Quad | Polygon::Shaded)) {
            const struct G *g = reinterpret_cast<G *>(m_command_fifo.data());

            for (size_t i = 0; i < 4; ++i) {
                vertices[i].x = SignExtend<11>(g->rgbxy[i].xy);
                vertices[i].y = SignExtend<11>(g->rgbxy[i].xy >> 16);

                vertices[i].r = g->rgbxy[i].rgb & 0xff;
                vertices[i].g = (g->rgbxy[i].rgb >> 8) & 0xff;
                vertices[i].b = (g->rgbxy[i].rgb >> 16) & 0xff;
            }
        }

        if constexpr (p == (Polygon::Quad | Polygon::Shaded | Polygon::Textured)) {
            const struct GT *gt = reinterpret_cast<GT *>(m_command_fifo.data());

            for (size_t i = 0; i < 4; ++i) {
                vertices[i].x = SignExtend<11>(gt->rgbxyuv[i].xy);
                vertices[i].y = SignExtend<11>(gt->rgbxyuv[i].xy >> 16);

                vertices[i].u = gt->rgbxyuv[i].uv & 0xff;
                vertices[i].v = (gt->rgbxyuv[i].uv >> 8) & 0xff;

                vertices[i].r = gt->rgbxyuv[i].rgb & 0xff;
                vertices[i].g = (gt->rgbxyuv[i].rgb >> 8) & 0xff;
                vertices[i].b = (gt->rgbxyuv[i].rgb >> 16) & 0xff;
            }

            clut.x = (gt->rgbxyuv[0].uv >> 12) & 0x3f0;
            clut.y = (gt->rgbxyuv[0].uv >> 22) & 0x1ff;

            m_texpage.raw &= 0x3600;
            m_texpage.raw |= ((gt->rgbxyuv[1].uv >> 16) & 0x09ff); 
        }

        DrawTriangle<Settings>(vertices[0], vertices[1], vertices[2], clut);

        if constexpr ((Settings & Polygon::Quad) != 0) {
            DrawTriangle<Settings>(vertices[1], vertices[2], vertices[3], clut);
        }
    }

    template <size_t Settings>
    void DrawTriangle(Vertex v0, Vertex v1, Vertex v2, Clut clut)
    {
        v0.x += m_drawing_offset.x;
        v0.y += m_drawing_offset.y;

        v1.x += m_drawing_offset.x;
        v1.y += m_drawing_offset.y;

        v2.x += m_drawing_offset.x;
        v2.y += m_drawing_offset.y;

        if (v0.y > v1.y) std::swap(v0, v1);
        if (v0.y > v2.y) std::swap(v0, v2);
        if (v1.y > v2.y) std::swap(v1, v2);

        Color32 c;
        c.raw = 0;

        int32_t xl = v0.x << 16;
        int32_t xr = v0.x << 16;

        int32_t ul = v0.u << 16;
        int32_t ur = v0.u << 16;

        int32_t vl = v0.v << 16;
        int32_t vr = v0.v << 16;

        int32_t rl = v0.r << 16;
        int32_t rr = v0.r << 16;

        int32_t gl = v0.g << 16;
        int32_t gr = v0.g << 16;

        int32_t bl = v0.b << 16;
        int32_t br = v0.b << 16;

        int32_t dxldy = 0;
        int32_t dxrdy = 0;

        int32_t duldy = 0;
        int32_t durdy = 0;

        int32_t dvldy = 0;
        int32_t dvrdy = 0;

        int32_t drldy = 0;
        int32_t drrdy = 0;

        int32_t dgldy = 0;
        int32_t dgrdy = 0;

        int32_t dbldy = 0;
        int32_t dbrdy = 0;

        const int32_t area = (v1.x - v0.x) * (v2.y - v0.y)
                             - (v2.x - v0.x) * (v1.y - v0.y);

        if (v0.y != v1.y) {
            dxldy = ((v2.x - v0.x) << 16) / (v2.y - v0.y);
            dxrdy = ((v1.x - v0.x) << 16) / (v1.y - v0.y);

            duldy = ((v2.u - v0.u) << 16) / (v2.y - v0.y);
            durdy = ((v1.u - v0.u) << 16) / (v1.y - v0.y);

            dvldy = ((v2.v - v0.v) << 16) / (v2.y - v0.y);
            dvrdy = ((v1.v - v0.v) << 16) / (v1.y - v0.y);

            drldy = ((v2.r - v0.r) << 16) / (v2.y - v0.y);
            drrdy = ((v1.r - v0.r) << 16) / (v1.y - v0.y);

            dgldy = ((v2.g - v0.g) << 16) / (v2.y - v0.y);
            dgrdy = ((v1.g - v0.g) << 16) / (v1.y - v0.y);
 
            dbldy = ((v2.b - v0.b) << 16) / (v2.y - v0.y);
            dbrdy = ((v1.b - v0.b) << 16) / (v1.y - v0.y); 
        }

        if (area < 0) {
            std::swap(dxldy, dxrdy);
            std::swap(duldy, durdy);
            std::swap(dvldy, dvrdy);
            std::swap(drldy, drrdy);
            std::swap(dgldy, dgrdy);
            std::swap(dbldy, dbrdy);
        }

        for (int32_t y = v0.y; y < v1.y; ++y) {
            const int32_t dx = std::max((xr - xl) >> 16, 1);

            const int32_t dudx = (ur - ul) / dx;
            const int32_t dvdx = (vr - vl) / dx;
            const int32_t drdx = (rr - rl) / dx;
            const int32_t dgdx = (gr - gl) / dx;
            const int32_t dbdx = (br - bl) / dx;

            int32_t u = ul;
            int32_t v = vl;
            int32_t r = rl;
            int32_t g = gl;
            int32_t b = bl;

            for (int32_t x = xl >> 16; x <= (xr >> 16); ++x) {
                c.r = r >> 16;
                c.g = g >> 16;
                c.b = b >> 16;

                if constexpr ((Settings & Polygon::Textured) != 0) {
                    Color16 d;
                    d.raw = FetchTexel(u >> 16, v >> 16, clut);

                    if (d.raw == 0) {
                        u += dudx;
                        v += dvdx;
                        r += drdx;
                        g += dgdx;
                        b += dbdx;
                        continue;
                    }

                    c.r = d.r << 3;
                    c.g = d.g << 3;
                    c.b = d.b << 3;
                    c.a = d.a;
                }

                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    DrawPixel<Settings>(x, y, c);
                }

                u += dudx;
                v += dvdx;
                r += drdx;
                g += dgdx;
                b += dbdx;
            }

            xl += dxldy;
            xr += dxrdy;

            ul += duldy;
            ur += durdy;

            vl += dvldy;
            vr += dvrdy;

            rl += drldy;
            rr += drrdy;

            gl += dgldy;
            gr += dgrdy;

            bl += dbldy;
            br += dbrdy;
        }

        xl = v2.x << 16;
        xr = v2.x << 16;

        ul = v2.u << 16;
        ur = v2.u << 16;

        vl = v2.v << 16;
        vr = v2.v << 16;

        rl = v2.r << 16;
        rr = v2.r << 16;

        gl = v2.g << 16;
        gr = v2.g << 16;

        bl = v2.b << 16;
        br = v2.b << 16;

        dxldy = dxrdy = 0;

        if (v1.y != v2.y) {
            dxldy = ((v1.x - v2.x) << 16) / (v2.y - v1.y);
            dxrdy = ((v0.x - v2.x) << 16) / (v2.y - v0.y);
 
            duldy = ((v1.u - v2.u) << 16) / (v2.y - v1.y);
            durdy = ((v0.u - v2.u) << 16) / (v2.y - v0.y);
 
            dvldy = ((v1.v - v2.v) << 16) / (v2.y - v1.y);
            dvrdy = ((v0.v - v2.v) << 16) / (v2.y - v0.y);

            drldy = ((v1.r - v2.r) << 16) / (v2.y - v1.y);
            drrdy = ((v0.r - v2.r) << 16) / (v2.y - v0.y);

            dgldy = ((v1.g - v2.g) << 16) / (v2.y - v1.y);
            dgrdy = ((v0.g - v2.g) << 16) / (v2.y - v0.y);
 
            dbldy = ((v1.b - v2.b) << 16) / (v2.y - v1.y);
            dbrdy = ((v0.b - v2.b) << 16) / (v2.y - v0.y);
      }

        if (area > 0) {
            std::swap(dxldy, dxrdy);
            std::swap(duldy, durdy);
            std::swap(dvldy, dvrdy);
            std::swap(drldy, drrdy);
            std::swap(dgldy, dgrdy);
            std::swap(dbldy, dbrdy);
        }

        for (int32_t y = v2.y; y >= v1.y; --y) {
            const int32_t dx = std::max((xr - xl) >> 16, 1);
            
            const int32_t dudx = (ur - ul) / dx;
            const int32_t dvdx = (vr - vl) / dx;
            const int32_t drdx = (rr - rl) / dx;
            const int32_t dgdx = (gr - gl) / dx;
            const int32_t dbdx = (br - bl) / dx;

            int32_t u = ul;
            int32_t v = vl;
            int32_t r = rl;
            int32_t g = gl;
            int32_t b = bl;

            for (int32_t x = xl >> 16; x <= (xr >> 16); ++x) {
                c.r = r >> 16;
                c.g = g >> 16;
                c.b = b >> 16;

                if constexpr ((Settings & Polygon::Textured) != 0) {
                    Color16 d;
                    d.raw = FetchTexel(u >> 16, v >> 16, clut);

                    if (d.raw == 0) {
                        u += dudx;
                        v += dvdx;
                        r += drdx;
                        g += dgdx;
                        b += dbdx;
                        continue;
                    }

                    c.r = d.r << 3;
                    c.g = d.g << 3;
                    c.b = d.b << 3;
                    c.a = d.a;
                }

                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    DrawPixel<Settings>(x, y, c);
                }

                u += dudx;
                v += dvdx;
                r += drdx;
                g += dgdx;
                b += dbdx;
            }

            xl += dxldy;
            xr += dxrdy;

            ul += duldy;
            ur += durdy;

            vl += dvldy;
            vr += dvrdy;

            rl += drldy;
            rr += drrdy;

            gl += dgldy;
            gr += dgrdy;

            bl += dbldy;
            br += dbrdy;
        } 
    }

    static constexpr size_t CommandFifoSize = 16;

    static constexpr size_t VramWidth = 1024;
    static constexpr size_t VramHeight = 512;

    uint32_t m_gpuread;

    enum class SemiTransparency : uint8_t { Average, Add, Sub, AddQuarter };
    enum class TextureFormat : uint8_t { I4, I8, ABGR1555, Reserved };

    enum class Field : bool { Even, Odd };

    enum class HorizontalResolution : uint8_t { H256, H320, H512, H640 };
    enum class VerticalResolution : bool { V240, V480 };
    enum class VideoMode : bool { NTSC, PAL };
    enum class PixelFormat : bool { XBGR1555, BGR888 };

    enum class DmaMode : uint8_t { Off, Fifo, CpuToGpu, GpuToCpu } m_dma_mode;

    union {
        uint32_t raw;

        BitField<uint32_t, uint32_t, 0, 4> texture_page_x;
        BitField<uint32_t, uint32_t, 4, 1> texture_page_y;
        BitField<uint32_t, SemiTransparency, 5, 2> semi_transparency;
        BitField<uint32_t, TextureFormat, 7, 2> texture_format;
        BitField<uint32_t, bool, 9, 1> dither;
        BitField<uint32_t, bool, 10, 1> draw_to_active_field; 
        BitField<uint32_t, bool, 11, 1> set_mask;
        BitField<uint32_t, bool, 12, 1> check_mask;
        BitField<uint32_t, Field, 13, 1> interlace_field1;
        BitField<uint32_t, bool, 14, 1> reverse_fields;
        BitField<uint32_t, bool, 15, 1> texture_disable;
        BitField<uint32_t, bool, 16, 1> force_hres_368px;
        BitField<uint32_t, HorizontalResolution, 17, 2> hres;
        BitField<uint32_t, VerticalResolution, 19, 1> vres;
        BitField<uint32_t, VideoMode, 20, 1> video_mode;
        BitField<uint32_t, PixelFormat, 21, 1> pixel_format;
        BitField<uint32_t, bool, 22, 1> vertical_interlace;
        BitField<uint32_t, bool, 23, 1> display_enable;
        BitField<uint32_t, bool, 24, 1> irq;
        BitField<uint32_t, bool, 25, 1> dma_request;
        BitField<uint32_t, bool, 26, 1> command_word_ready;
        BitField<uint32_t, bool, 27, 1> vram_read_ready;
        BitField<uint32_t, bool, 28, 1> dma_block_ready;
        BitField<uint32_t, DmaMode, 29, 2> dma_mode;
        BitField<uint32_t, Field, 31, 1> interlace_field2;
    } m_gpustat;

    union Texpage {
        uint16_t raw;

        BitField<uint16_t, uint16_t, 0, 4> texture_page_x;
        BitField<uint16_t, uint16_t, 4, 1> texture_page_y;
        BitField<uint16_t, SemiTransparency, 5, 2> semi_transparency;
        BitField<uint16_t, TextureFormat, 7, 2> texture_format;
        BitField<uint16_t, bool, 9, 1> dither;
        BitField<uint16_t, bool, 10, 1> draw_to_active_field;
        BitField<uint16_t, bool, 11, 1> texture_disable;
        BitField<uint16_t, bool, 12, 1> textured_rect_xflip;
        BitField<uint16_t, bool, 13, 1> textured_rect_yflip;
 
    } m_texpage;

    union {
        uint32_t raw;

        BitField<uint32_t, uint8_t, 0, 5> mask_x;
        BitField<uint32_t, uint8_t, 5, 5> mask_y;
        BitField<uint32_t, uint8_t, 10, 5> offset_x;
        BitField<uint32_t, uint8_t, 15, 5> offset_y;
    } m_texture_window;

    union {
        uint32_t raw;

        BitField<uint32_t, uint32_t, 0, 10> x;
        BitField<uint32_t, uint32_t, 10, 9> y;
    } m_drawing_area_start, m_drawing_area_end;

    union {
        uint32_t raw;

        BitField<uint32_t, int32_t, 0, 11> x;
        BitField<uint32_t, int32_t, 11, 11> y;
    } m_drawing_offset;

    union {
        uint8_t raw;

        BitField<uint8_t, bool, 0, 1> set;
        BitField<uint8_t, bool, 1, 1> check;
    } m_mask_bit;

    bool m_display_enable;

    union {
        uint32_t raw;

        BitField<uint32_t, uint32_t, 0, 10> x;
        BitField<uint32_t, uint32_t, 10, 9> y;
    } m_display_area_origin;

    union {
        uint32_t raw;

        BitField<uint32_t, uint32_t, 0, 12> start;
        BitField<uint32_t, uint32_t, 12, 12> end;
    } m_horizontal_display_range;

    union {
        uint32_t raw;

        BitField<uint32_t, uint32_t, 0, 10> start;
        BitField<uint32_t, uint32_t, 10, 10> end;
    } m_vertical_display_range;

    union {
        uint8_t raw;

        BitField<uint8_t, HorizontalResolution, 0, 2> hres;
        BitField<uint8_t, VerticalResolution, 2, 1> vres;
        BitField<uint8_t, VideoMode, 3, 1> video_mode;
        BitField<uint8_t, PixelFormat, 4, 1> pixel_format;
        BitField<uint8_t, bool, 5, 1> vertical_interlace;
        BitField<uint8_t, bool, 6, 1> force_hres_368px;
        BitField<uint8_t, bool, 7, 1> reverse_fields;
    } m_display_mode;

    bool m_receiving_parameters;
    size_t m_parameters_remaining;

    enum class TransferMode { Fifo, Read, Write };

    struct {
        TransferMode mode;
        size_t x, y, w, h, tx, ty;
    } m_transfer;

    size_t m_command_fifo_size;
    std::array<uint32_t, CommandFifoSize> m_command_fifo;

    std::array<uint16_t, VramWidth * VramHeight> m_vram;
};

}

#endif /* CORE_GPU_HPP */
