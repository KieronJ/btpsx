#ifndef CORE_SPU_HPP
#define CORE_SPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include <common/bitfield.hpp>
#include <common/cbuf.hpp>

namespace Core
{

class Spu {
public:
    Spu(bool enable_audio);

    void Reset();

    void Tick();

    inline Cbuf<int16_t, 8192> * SoundFifo()
    {
        return &m_sound_fifo;
    }

    uint16_t Read(uint32_t addr);
    void Write(uint32_t addr, uint16_t data);

    void WriteDma(uint32_t data);

private:
    void KeyOn(uint32_t value);
    void KeyOff(uint32_t value);

    static constexpr size_t SoundRamSize = 512 * 512;
    static constexpr size_t SoundBufferSize = 256;

    static constexpr int Filter1[] = { 0, 60, 115, 98, 122 };
    static constexpr int Filter2[] = { 0, 0, -52, -55, -60 };

    enum class State { Attack, Decay, Sustain, Release, Off };

    enum class AdsrMode : bool { Linear, Exponential };
    enum class AdsrDirection : bool { Increase, Decrease };

    struct Voice {
        struct {
            int16_t l, r;
        } volume;

        uint16_t pitch;
        uint16_t address;

        union {
            uint32_t raw;

            BitField<uint32_t, uint16_t, 0, 16> l;
            BitField<uint32_t, uint16_t, 16, 16> h;

            BitField<uint32_t, uint32_t, 0, 4> sustain_level;
            BitField<uint32_t, int, 4, 4> decay_shift;
            BitField<uint32_t, uint32_t, 8, 2> attack_step;
            BitField<uint32_t, int, 10, 5> attack_shift;
            BitField<uint32_t, AdsrMode, 15, 1> attack_mode;
            BitField<uint32_t, int, 16, 5> release_shift;
            BitField<uint32_t, AdsrMode, 21, 1> release_mode;
            BitField<uint32_t, uint32_t, 22, 2> sustain_step;
            BitField<uint32_t, int, 24, 5> sustain_shift;
            BitField<uint32_t, AdsrDirection, 30, 1> sustain_direction;
            BitField<uint32_t, AdsrMode, 31, 1> sustain_mode;
        } adsr;

        size_t adsr_counter;
        int16_t adsr_volume;

        uint16_t repeat_address;

        bool header_processed;

        size_t current_address;
        size_t counter, sample;

        union {
            uint16_t raw;

            BitField<uint16_t, uint16_t, 0, 4> range;
            BitField<uint16_t, uint16_t, 4, 3> filter;
            BitField<uint16_t, bool, 8, 1> end;
            BitField<uint16_t, bool, 9, 1> loop;
            BitField<uint16_t, bool, 10, 1> start;
        } header;

        int filter1, filter2;

        int16_t prev_sample[2];

        State state;
    } m_voices[24];

    void AdsrStep(Voice& voice);
    int16_t DecodeSample(Voice& voice, int16_t data);

    union {
        uint32_t raw;

        BitField<uint32_t, uint16_t, 0, 16> l;
        BitField<uint32_t, uint16_t, 16, 16> h;
    } m_pitch_mod_on, m_noise_on, m_effect_on;

    struct {
        int16_t l, r;
    } m_master_volume, m_effect_volume, m_cd_volume, m_external_volume;

    uint16_t m_transfer_addr;
    size_t m_transfer_current_addr;

    uint32_t m_endx;

    uint16_t m_transfer_control;

    enum class TransferMode : uint8_t { Off, ManualWrite, DmaWrite, DmaRead };

    union {
        uint16_t raw;

        BitField<uint16_t, bool, 0, 1> cd_enable;
        BitField<uint16_t, bool, 1, 1> external_enable;
        BitField<uint16_t, bool, 2, 1> cd_effect_enable;
        BitField<uint16_t, bool, 3, 1> external_effect_enable;
        BitField<uint16_t, TransferMode, 4, 2> transfer_mode;
        BitField<uint16_t, bool, 6, 1> interrupt_enable;
        BitField<uint16_t, bool, 7, 1> effect_enable;
        BitField<uint16_t, bool, 14, 1> master_mute;
        BitField<uint16_t, bool, 15, 1> master_enable;
    } m_control;

    union {
        uint16_t raw;

        BitField<uint16_t, bool, 0, 1> cd_enable;
        BitField<uint16_t, bool, 1, 1> external_enable;
        BitField<uint16_t, bool, 2, 1> cd_effect_enable;
        BitField<uint16_t, bool, 3, 1> external_effect_enable;
        BitField<uint16_t, TransferMode, 4, 2> transfer_mode;
        BitField<uint16_t, bool, 6, 1> interrupt_request;
        BitField<uint16_t, bool, 7, 1> dma_request;
        BitField<uint16_t, bool, 8, 1> dma_write_request;
        BitField<uint16_t, bool, 9, 1> dma_read_request;
        BitField<uint16_t, bool, 10, 1> dma_busy;
    } m_status;

    uint16_t m_effect_base;

    std::array<uint16_t, SoundRamSize> m_sound_ram;

    size_t m_sound_buffer_index = 0;
    std::array<int16_t, SoundBufferSize> m_sound_buffer;

    bool m_enable_audio;
    Cbuf<int16_t, 8192> m_sound_fifo;
};

}

#endif /* CORE_SPU_HPP */
