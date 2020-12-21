#include <cstdint>

#include <spdlog/spdlog.h>

#include "error.hpp"
#include "gpu.hpp"

namespace Core
{

Gpu::Gpu() { Reset(); }

void Gpu::Reset()
{
    m_gpustat.interlace_field2 = Field::Even;
    m_receiving_parameters = false;
    m_command_fifo_size = 0;
    m_transfer.mode = TransferMode::Fifo;
    m_texpage.raw = 0;
    m_texture_window.raw = 0;
    m_drawing_area_start.raw = 0;
    m_drawing_area_end.raw = 0;
    m_drawing_offset.raw = 0;
    m_mask_bit.raw = 0;
    m_display_enable = false;
    m_dma_mode = DmaMode::Off;
    m_display_area_origin.raw = 0;
    m_horizontal_display_range.start = 512;
    m_horizontal_display_range.end = 3072;
    m_vertical_display_range.start = 16;
    m_vertical_display_range.end = 256;
    m_display_mode.raw = 0;

    UpdateGpustat();
}

uint32_t Gpu::GpuRead()
{
    if (m_transfer.mode == TransferMode::Read) {
        uint32_t data = 0;

        for (size_t i = 0; i < 2; ++i) {
            const uint16_t d = ReadVram(m_transfer.x + m_transfer.tx,
                                        m_transfer.y + m_transfer.ty);
            data |= d << (16 * i);

            if (++m_transfer.tx == m_transfer.w) {
                m_transfer.tx = 0;

               if (++m_transfer.ty == m_transfer.h) {
                    m_transfer.ty = 0;

                    m_transfer.mode = TransferMode::Fifo;
                }
            }
        }

        return data;
    }

    return m_gpuread;
}

uint32_t Gpu::GpuStat()
{
    m_gpustat.raw ^= 0x80000000;
    return m_gpustat.raw;
}

void Gpu::Gp0(uint32_t data)
{
    if (m_transfer.mode == TransferMode::Write) {
        for(size_t i = 0; i < 2; ++i) {
            WriteVram((m_transfer.x + m_transfer.tx) & 0x3ff,
                      (m_transfer.y + m_transfer.ty) & 0x1ff,
                      data >> (16 * i));

            if (++m_transfer.tx == m_transfer.w) {
                m_transfer.tx = 0;

                if (++m_transfer.ty == m_transfer.h) {
                    m_transfer.mode = TransferMode::Fifo;
                    return;
                }
            }
        }

        return;
    }

    if (m_receiving_parameters) {
        if (m_command_fifo_size >= CommandFifoSize) {
            Error("gpu command fifo overflow");
        }

        m_command_fifo[m_command_fifo_size++] = data;

        if (--m_parameters_remaining == 0) {
            ExecuteCommand();

            m_receiving_parameters = false;
            m_command_fifo_size = 0;
        }

        return;
    }

    const uint8_t command = data >> 24;

    switch (command) {
    case 0x00:
        if (data != 0) {
            spdlog::debug("gp0(00h) junk 0x{:06x}", data);
        }

        break;
    case 0x01:
        /* TODO: texture cache */
        break;
    case 0x02:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 2;
        break;
    case 0x20:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 3;
        break;
    case 0x28:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 4;
        break;
    case 0x2a:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 4;
        break;
    case 0x2c:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 8;
        break;
    case 0x2d:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 8;
        break;
    case 0x30:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 5;
        break;
    case 0x38:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 7;
        break;
    case 0x60:
    case 0x62:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 2;
        break;
    case 0x64: case 0x65:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 3;
        break;
    case 0x68:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 1;
        break;
    case 0x7c:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 2;
        break;
    case 0x80:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 3;
        break;
    case 0xa0: 
    case 0xc0:
        m_command_fifo[m_command_fifo_size++] = data;

        m_receiving_parameters = true;
        m_parameters_remaining = 2;
        break;
    case 0xe1:
        m_texpage.raw = data & 0x3fff;
        UpdateGpustat();
        break;
    case 0xe2:
        m_texture_window.raw = data & 0xfffff;
        break;
    case 0xe3:
        m_drawing_area_start.raw = data & 0x7ffff;
        break;
    case 0xe4:
        m_drawing_area_end.raw = data & 0x7ffff;
        break;
    case 0xe5:
        m_drawing_offset.raw = data & 0x3fffff;
        break;
    case 0xe6:
        m_mask_bit.raw = data & 0x3;
        UpdateGpustat();
        break;
    default: Error("unknown gp0 command 0x{:02x}", command);
    }
}

void Gpu::Gp1(uint32_t data)
{
    const uint8_t command = data >> 24;

    switch (command) {
    case 0x00:
        Reset();
        break;
    case 0x01:
        m_receiving_parameters = false;
        m_command_fifo_size = 0;
        break;
    case 0x02:
        /* TODO: irq */
        break;
    case 0x03:
        m_display_enable = (data & 0x1) == 0;
        UpdateGpustat();
        break;
    case 0x04:
        m_dma_mode = static_cast<DmaMode>(data & 0x3);
        UpdateGpustat();
        break;
    case 0x05:
        m_display_area_origin.raw = data & 0x7ffff;
        break;
    case 0x06:
        m_horizontal_display_range.raw = data & 0xffffff;
        break;
    case 0x07:
        m_vertical_display_range.raw = data & 0xfffff;
        break;
    case 0x08:
        m_display_mode.raw = data;
        UpdateGpustat();
        break;
    case 0x10:
        switch (data & 0x7) {
        case 0x0:
        case 0x1:
        case 0x6:
        case 0x7:
            break;
        case 0x2:
            m_gpuread = m_texture_window.raw;
            break;
        case 0x3:
            m_gpuread = m_drawing_area_start.raw;
            break;
        case 0x4:
            m_gpuread = m_drawing_area_end.raw;
            break;
        case 0x5:
            m_gpuread = m_drawing_offset.raw;
            break;
        }

        break;
    default: Error("unknown gp1 command 0x{:02x}", command);
    }
}

#define POLY(n) case n: DrawPolygon<static_cast<Polygon>(n)>(); break;

void Gpu::ExecuteCommand()
{
    const uint8_t command = m_command_fifo[0] >> 24;

    union {
        uint16_t raw;

        BitField<uint16_t, uint8_t, 0, 5> r;
        BitField<uint16_t, uint8_t, 5, 5> g;
        BitField<uint16_t, uint8_t, 10, 5> b;
        BitField<uint16_t, bool, 15, 1> a;
    } c;

    c.raw = 0;
    c.r = m_command_fifo[0] >> 3;
    c.g = m_command_fifo[0] >> 11;
    c.b = m_command_fifo[0] >> 19;

    int16_t x = SignExtend<11>(m_command_fifo[1]);
    int16_t y = SignExtend<11>(m_command_fifo[1] >> 16);
    uint16_t w = m_command_fifo[2] & 0xffff;
    uint16_t h = m_command_fifo[2] >> 16;

    uint8_t u, v;

    uint16_t srcx, srcy, dstx, dsty;

    Clut clut;

    switch (command) {
    case 0x02:
        x &= 0x3f0;
        y &= 0x1ff;
        w = ((w & 0x3ff) + 0xf) & ~0xf;
        h &= 0x1ff;

        for (size_t ty = 0; ty < h; ++ty) {
            for (size_t tx = 0; tx < w; ++tx) {
                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    WriteVram(x + tx, y + ty, c.raw);
                }
            }
        }

        break;
    POLY(0x20);
    POLY(0x28);
    POLY(0x2a);
    POLY(0x2c);
    POLY(0x2d);
    POLY(0x30);
    POLY(0x38);
    case 0x60:
    case 0x62:
        x += m_drawing_offset.x;
        y += m_drawing_offset.y;

        w = m_command_fifo[3] & 0xffff;
        h = m_command_fifo[3] >> 16;

        for (size_t ty = 0; ty < h; ++ty) {
            for (size_t tx = 0; tx < w; ++tx) {
                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    WriteVram((x + tx) & 0x3ff, (y + ty) & 0x1ff, c.raw);
                }
            }
        }

        break;
    case 0x64:
    case 0x65:
        x += m_drawing_offset.x;
        y += m_drawing_offset.y;

        u = m_command_fifo[2];
        v = m_command_fifo[2] >> 8;

        clut.x = (m_command_fifo[2] >> 12) & 0x3f0;
        clut.y = (m_command_fifo[2] >> 22) & 0x1ff;

        w = m_command_fifo[3] & 0xffff;
        h = m_command_fifo[3] >> 16;

        for (size_t ty = 0; ty < h; ++ty) {
            for (size_t tx = 0; tx < w; ++tx) {
                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    WriteVram(x + tx, y + ty, FetchTexel(u + tx, v + ty, clut));
                }
            }
        }

        break;
    case 0x68:
        x += m_drawing_offset.x;
        y += m_drawing_offset.y;
        WriteVram(x, y, c.raw);
        break;
    case 0x7c:
        x += m_drawing_offset.x;
        y += m_drawing_offset.y;

        u = m_command_fifo[2];
        v = m_command_fifo[2] >> 8;

        clut.x = (m_command_fifo[2] >> 12) & 0x3f0;
        clut.y = (m_command_fifo[2] >> 22) & 0x1ff;

        for (size_t ty = 0; ty < 16; ++ty) {
            for (size_t tx = 0; tx < 16; ++tx) {
                if (x >= m_drawing_area_start.x && x <= m_drawing_area_end.x
                    && y >= m_drawing_area_start.y && y <= m_drawing_area_end.y) {
                    WriteVram(x + tx, y + ty, FetchTexel(u + tx, v + ty, clut));
                }
            }
        }

        break;
    case 0x80:
        srcx = m_command_fifo[1];
        srcy = m_command_fifo[1] >> 16;
        dstx = m_command_fifo[2];
        dsty = m_command_fifo[2] >> 16;
        w = m_command_fifo[2];
        h = m_command_fifo[2] >> 16;

        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                WriteVram(dstx + x, dsty + y, ReadVram(srcx + x, srcy + y));
            }
        }

        break;
    case 0xa0:
    case 0xc0:
        if (m_transfer.mode != TransferMode::Fifo) {
            Error("gpu transfer overlap");
        }

        m_transfer.mode = (command == 0xa0) ? TransferMode::Write : TransferMode::Read;
        m_transfer.x = x & 1023;
        m_transfer.y = y & 511;
        m_transfer.w = ((w - 1) & 1023) + 1;
        m_transfer.h = ((h - 1) & 511) + 1;
        m_transfer.tx = 0;
        m_transfer.ty = 0;

        break;
    default: Error("unknown gp0 command 0x{:02x}", command);
    }
}

#undef POLY

void Gpu::UpdateGpustat()
{
    m_gpustat.texture_page_x = m_texpage.texture_page_x;
    m_gpustat.texture_page_y = m_texpage.texture_page_y;
    m_gpustat.semi_transparency = m_texpage.semi_transparency;
    m_gpustat.texture_format = m_texpage.texture_format;
    m_gpustat.dither = m_texpage.dither;
    m_gpustat.draw_to_active_field = m_texpage.draw_to_active_field;
    m_gpustat.set_mask = m_mask_bit.set;
    m_gpustat.check_mask = m_mask_bit.check;

    if (m_display_mode.vertical_interlace) {
        m_gpustat.interlace_field1 = m_gpustat.interlace_field2;
    } else {
        m_gpustat.interlace_field1 = Field::Odd;
    }

    m_gpustat.reverse_fields = m_display_mode.reverse_fields;
    m_gpustat.texture_disable = m_texpage.texture_disable;
    m_gpustat.force_hres_368px = m_display_mode.force_hres_368px;
    m_gpustat.hres = m_display_mode.hres;
    m_gpustat.vres = m_display_mode.vres;
    m_gpustat.video_mode = m_display_mode.video_mode;
    m_gpustat.pixel_format = m_display_mode.pixel_format;
    m_gpustat.vertical_interlace = m_display_mode.vertical_interlace;
    m_gpustat.display_enable = m_display_enable;
    m_gpustat.irq = false;
    m_gpustat.dma_request = true;
    m_gpustat.command_word_ready = true;
    m_gpustat.vram_read_ready = true;
    m_gpustat.dma_block_ready = true;
    m_gpustat.dma_mode = m_dma_mode;
}

uint16_t Gpu::FetchTexel(uint8_t u, uint8_t v, Clut clut)
{
    const size_t xbase = 64 * m_texpage.texture_page_x;
    const size_t ybase = 256 * m_texpage.texture_page_y;

    uint16_t index;

    switch (m_texpage.texture_format) {
    case TextureFormat::I4:
        index = ReadVram((xbase + (u / 4)) & 0x3ff, (ybase + v) & 0x1ff) >> (4 * (u & 0x3));
        return ReadVram((clut.x + (index & 0xf)) & 0x3ff, clut.y & 0x1ff);
    case TextureFormat::I8:
        index = ReadVram((xbase + (u / 2)) & 0x3ff, (ybase + v) & 0x1ff) >> (8 * (u & 0x1));
        return ReadVram((clut.x + (index & 0xff)) & 0x3ff, clut.y & 0x1ff);
    case TextureFormat::ABGR1555:
        return ReadVram((xbase + u) & 0x3ff, (ybase + v) & 0x1ff);
    default: Error("unimplemented texture format");
    }
}

}
