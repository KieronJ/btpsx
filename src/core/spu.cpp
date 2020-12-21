#include <cstdint>
#include <mutex>

#include <common/signextend.hpp>

#include <spdlog/spdlog.h>

#include "error.hpp"
#include "spu.hpp"

namespace Core
{

Spu::Spu(bool enable_audio) : m_enable_audio{enable_audio} {}

void Spu::Reset()
{
    KeyOff(0xffffffff);
}

void Spu::AdsrStep(Voice& voice)
{
    if (--voice.adsr_counter != 0) {
        return;
    }

    int step, step_shift, shift;

    switch (voice.state) {
    case State::Attack: 
        step_shift = std::max(0, 11 - voice.adsr.attack_shift);
        step = (7 - voice.adsr.attack_step) << step_shift;
        voice.adsr_volume = std::clamp(int32_t(voice.adsr_volume) + step, 0, INT16_MAX);

        if (voice.adsr_volume == INT16_MAX) {
            voice.state = State::Decay;

            shift = voice.adsr.decay_shift - 11;
            voice.adsr_counter = 1 << std::max(0, shift);
            voice.adsr_counter = (voice.adsr_counter * voice.adsr_volume) >> 15;
        } else {
            shift = voice.adsr.attack_shift - 11;
            voice.adsr_counter = 1 << std::max(0, shift);

            if (voice.adsr.attack_mode == AdsrMode::Exponential
                && voice.adsr_volume > 0x6000) {
                voice.adsr_counter *= 4;
            }
        }

        break;
    case State::Decay:
        step_shift = std::max(0, 11 - voice.adsr.decay_shift);
        step = -8 << step_shift;
        step = (step * voice.adsr_volume) >> 15;

        voice.adsr_volume = std::clamp(int32_t(voice.adsr_volume) + step, 0, INT16_MAX);

        if (voice.adsr_volume <= 0x800 * (voice.adsr.sustain_level + 1)) {
            voice.state = State::Sustain;

            voice.adsr_volume = 0x800 * (voice.adsr.sustain_level + 1);

            shift = voice.adsr.sustain_shift - 11;
            voice.adsr_counter = 1 << std::max(0, shift); 

            if (voice.adsr.sustain_mode == AdsrMode::Exponential
                && voice.adsr.sustain_direction == AdsrDirection::Increase
                && voice.adsr_volume > 0x6000) {
                voice.adsr_counter *= 4;
            }
        } else {
            shift = voice.adsr.decay_shift - 11;
            voice.adsr_counter = 1 << std::max(0, shift);
        }

        break;
    case State::Sustain:
        step_shift = std::max(0, 11 - voice.adsr.sustain_shift);
        step = 7 - voice.adsr.attack_step;

        if (voice.adsr.sustain_direction == AdsrDirection::Decrease) {
            step = ~step;
        }

        step <<= step_shift;

        if (voice.adsr.sustain_direction == AdsrDirection::Decrease
            && voice.adsr.sustain_mode == AdsrMode::Exponential) {
            step = (step * voice.adsr_volume) >> 15;
        }

        voice.adsr_volume = std::clamp(int32_t(voice.adsr_volume) + step, 0, INT16_MAX);
 
        shift = voice.adsr.sustain_shift - 11;
        voice.adsr_counter = 1 << std::max(0, shift); 

        if (voice.adsr.sustain_mode == AdsrMode::Exponential
            && voice.adsr.sustain_direction == AdsrDirection::Increase
            && voice.adsr_volume > 0x6000) {
            voice.adsr_counter *= 4;
        } 

        break;
    case State::Release:
        step_shift = std::max(0, 11 - voice.adsr.release_shift);
        step = -8 << step_shift;

        if (voice.adsr.release_mode == AdsrMode::Exponential) {
            step = (step * voice.adsr_volume) >> 15;
        } 

        voice.adsr_volume = std::clamp(int32_t(voice.adsr_volume) + step, 0, INT16_MAX);

        if (voice.adsr_volume == 0) {
            voice.state = State::Off;
        } else {
            shift = voice.adsr.release_shift - 11;
            voice.adsr_counter = 1 << std::max(0, shift);
        }

        break;
    case State::Off:
        Error("adsr tick whilst voice disabled");
        break;
   }
}

int16_t Spu::DecodeSample(Voice& voice, int16_t data)
{
    int32_t sample = SignExtend<4>(data);
    int32_t prev = 0;

    prev += voice.prev_sample[0] * voice.filter1;
    prev += voice.prev_sample[1] * voice.filter2;

    sample = (sample << (12 - voice.header.range)) + prev / 64;
    sample = std::clamp(sample, INT16_MIN, INT16_MAX);

    voice.prev_sample[1] = voice.prev_sample[0];
    voice.prev_sample[0] = sample;

    return sample;
}

void Spu::Tick()
{
    int16_t l = 0;
    int16_t r = 0;

    for (size_t i = 0; i < 24; ++i) {
        Voice& voice = m_voices[i];

        if (voice.state == State::Off) {
            continue;
        }

        if (!voice.header_processed) {
            voice.header.raw = m_sound_ram[voice.current_address];

            if (voice.header.start) {
                voice.repeat_address = voice.current_address / 4;
            }

            if (voice.header.range > 12) {
                Error("invalid spu range {}", voice.header.range);
            }

            if (voice.header.filter > 4) {
                Error("invalid spu filter {}", voice.header.filter);
            }

            voice.filter1 = Filter1[voice.header.filter];
            voice.filter2 = Filter2[voice.header.filter];

            voice.header_processed = true;
        } 

        uint16_t data = m_sound_ram[voice.current_address + 1 + voice.sample / 4];
        int16_t sample = DecodeSample(voice, data >> 4 * (voice.sample & 3));

        /* TODO: pitch modulation */
        const uint16_t pitch = (voice.pitch > 0x4000) ? 0x4000 : voice.pitch;
        voice.counter += pitch;

        voice.sample += voice.counter >> 12;
        voice.counter &= 0xfff;

        if (voice.sample >= 28) {
            voice.sample -= 28;

            voice.current_address += 8;

            if (voice.header.end) {
                voice.current_address = 4 * voice.repeat_address;

                m_endx |= 1 << i;

                if (!voice.header.loop) {
                    voice.state = State::Off;
                    voice.adsr_volume = 0;
                }
            }

            voice.header_processed = false;
        }

        AdsrStep(voice);
        sample = (sample * voice.adsr_volume) >> 15;

        const int16_t samplel = (sample * voice.volume.l) >> 15;
        const int16_t sampler = (sample * voice.volume.r) >> 15;

        l = std::clamp(int32_t(l) + samplel, INT16_MIN, INT16_MAX);
        r = std::clamp(int32_t(r) + sampler, INT16_MIN, INT16_MAX);
    }

    l = (l * m_master_volume.l) >> 15;
    r = (r * m_master_volume.r) >> 15;

    m_sound_buffer[m_sound_buffer_index++] = l;
    m_sound_buffer[m_sound_buffer_index++] = r;

    if (m_sound_buffer_index == SoundBufferSize) {
        m_sound_buffer_index = 0;

        if (m_enable_audio) {
            m_sound_fifo.Enqueue(m_sound_buffer.data(), SoundBufferSize);
        }
    }
}

uint16_t Spu::Read(uint32_t addr)
{
    if (addr < 0x1f801d80) {
        const Voice& voice = m_voices[(addr - 0x1f801c00) >> 4];

        switch (addr & 0xf) {
        case 0x8: return voice.adsr.l;
        case 0xa: return voice.adsr.h;
        case 0xc: return voice.adsr_volume;
        default: Error("read from unknown spu reg 0x{:08x}", addr);
        }
    }

    switch (addr) {
    case 0x1f801d88: case 0x1f801d8a:
        spdlog::warn("read from write-only KON");
        return 0;
    case 0x1f801d8c: case 0x1f801d8e:
        spdlog::warn("read from write-only KOFF");
        return 0;
    case 0x1f801d94: return m_noise_on.l;
    case 0x1f801d96: return m_noise_on.h;
    case 0x1f801d98: return m_effect_on.l;
    case 0x1f801d9a: return m_effect_on.h;
    case 0x1f801d9c: return m_endx;
    case 0x1f801d9e: return m_endx >> 16;
    case 0x1f801da6: return m_transfer_addr;
    case 0x1f801daa: return m_control.raw;
    case 0x1f801dac: return m_transfer_control;
    case 0x1f801dae: return m_status.raw;
    case 0x1f801db8: case 0x1f801dba:
        spdlog::warn("read from unimplemented master current volume");
        return 0;
    default: Error("read from unknown spu reg 0x{:08x}", addr);
    }
}

void Spu::Write(uint32_t addr, uint16_t data)
{
    if (addr < 0x1f801d80) {
        Voice& voice = m_voices[(addr - 0x1f801c00) >> 4];

        switch (addr & 0xf) {
        case 0x0: voice.volume.l = data; break;
        case 0x2: voice.volume.r = data; break;
        case 0x4: voice.pitch = data; break;
        case 0x6: voice.address = data; break;
        case 0x8: voice.adsr.l = data; break;
        case 0xa: voice.adsr.h = data; break;
        case 0xc: voice.adsr_volume = data; break;
        case 0xe: voice.repeat_address = data; break;
        default: Error("write to unknown spu reg 0x{:08x}", addr);
        }

        return;
    }

    if (addr >= 0x1f801dc0 && addr < 0x1f801e00) {
        spdlog::warn("write to spu effect reg 0x{:08x}", addr);
        return;
    }

    switch (addr) {
    case 0x1f801d80: m_master_volume.l = data; break;
    case 0x1f801d82: m_master_volume.r = data; break;
    case 0x1f801d84: m_effect_volume.l = data; break;
    case 0x1f801d86: m_effect_volume.r = data; break;
    case 0x1f801d88: KeyOn(data); break;
    case 0x1f801d8a: KeyOn(data << 16); break;
    case 0x1f801d8c: KeyOff(data); break;
    case 0x1f801d8e: KeyOff(data << 16); break;
    case 0x1f801d90: m_pitch_mod_on.l = data; break;
    case 0x1f801d92: m_pitch_mod_on.h = data; break;
    case 0x1f801d94: m_noise_on.l = data; break;
    case 0x1f801d96: m_noise_on.h = data; break;
    case 0x1f801d98: m_effect_on.l = data; break;
    case 0x1f801d9a: m_effect_on.h = data; break;
    case 0x1f801d9c: case 0x1f801d9e:
        spdlog::warn("write to read-only ENDX");
        break;
    case 0x1f801da2: m_effect_base = data; break;
    case 0x1f801da6:
        m_transfer_addr = data;
        m_transfer_current_addr = 4 * m_transfer_addr;
        break;
    case 0x1f801da8:
        m_sound_ram[m_transfer_current_addr++] = data;
        m_transfer_current_addr &= 0x3ffff;
        break;
    case 0x1f801daa:
        m_control.raw = data;

        m_status.cd_enable = m_control.cd_enable;
        m_status.external_enable = m_control.external_enable;
        m_status.cd_effect_enable = m_control.cd_effect_enable;
        m_status.external_effect_enable = m_control.external_effect_enable;
        m_status.transfer_mode = m_control.transfer_mode;
        break;
    case 0x1f801dac: m_transfer_control = data; break;
    case 0x1f801db0: m_cd_volume.l = data; break;
    case 0x1f801db2: m_cd_volume.r = data; break;
    case 0x1f801db4: m_external_volume.l = data; break;
    case 0x1f801db6: m_external_volume.r = data; break;
    default: Error("write to unknown spu reg 0x{:08x}", addr);
    }
}

void Spu::WriteDma(uint32_t data)
{
    m_sound_ram[m_transfer_current_addr++] = data;
    m_transfer_current_addr &= 0x3ffff;

    m_sound_ram[m_transfer_current_addr++] = data >> 16;
    m_transfer_current_addr &= 0x3ffff;
}

void Spu::KeyOn(uint32_t value)
{
    for (size_t i = 0; i < 24; ++i) {
        if ((value & (1 << i)) != 0) {
            m_voices[i].state = State::Attack;

            const int shift = m_voices[i].adsr.attack_shift - 11;
            m_voices[i].adsr_counter = 1 << std::max(0, shift);
            m_voices[i].adsr_volume = 0;

            m_voices[i].header_processed = false;

            m_voices[i].current_address = 4 * m_voices[i].address;
            m_voices[i].repeat_address = m_voices[i].address;
            m_voices[i].counter = m_voices[i].sample = 0;
            m_voices[i].prev_sample[0] = m_voices[i].prev_sample[1] = 0;

            m_endx &= ~(1 << i);
        }
    }
}

void Spu::KeyOff(uint32_t value)
{
    for (size_t i = 0; i < 24; ++i) {
        if ((value & (1 << i)) != 0) {
            m_voices[i].state = State::Release;
        }
    }
}

}
