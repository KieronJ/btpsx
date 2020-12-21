#ifndef CORE_CDC_HPP
#define CORE_CDC_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <common/bitfield.hpp>

#include "disc/disc.hpp"

namespace Core
{

class Emulator;

class Cdc {
public:
    Cdc(Emulator *emulator, const std::filesystem::path& disc);

    void Reset();

    void Tick();

    uint8_t Read(uint32_t addr);
    void Write(uint32_t addr, uint8_t data);

    uint32_t ReadDma();

private:
    void ExecuteCommand();
    void ExecuteCommandSecondResponse();

    void ExecuteTestCommand();

    void DeliverDataSector();

    void FillDataFifo();

    static constexpr size_t ParameterFifoSize = 16;
    static constexpr size_t ResponseFifoSize = 16;

    static constexpr size_t DiscSectorSize = 2352;

    union {
        uint8_t raw;

        BitField<uint8_t, uint8_t, 0, 2> index;
        BitField<uint8_t, bool, 2, 1> adpbusy;
        BitField<uint8_t, bool, 3, 1> prmempt;
        BitField<uint8_t, bool, 4, 1> prmwrdy;
        BitField<uint8_t, bool, 5, 1> rslrrdy;
        BitField<uint8_t, bool, 6, 1> drqsts;
        BitField<uint8_t, bool, 7, 1> busysts;
    } m_status;

    enum class DriveState : uint8_t {
        None,
        Reading,
        Seeking,
        Playing = 0x4
    };

    union {
        uint8_t raw;

        BitField<uint8_t, bool, 0, 1> error;
        BitField<uint8_t, bool, 1, 1> motor_on;
        BitField<uint8_t, bool, 2, 1> seek_error;
        BitField<uint8_t, bool, 3, 1> id_error;
        BitField<uint8_t, bool, 4, 1> shell_open;
        BitField<uint8_t, DriveState, 5, 3> drive_state;
    } m_stat;

    enum class SectorSize : bool { DataOnly, WholeSector };
    enum class DriveSpeed : bool { Single, Double };

    union {
        uint8_t raw;

        BitField<uint8_t, bool, 0, 1> cdda;
        BitField<uint8_t, bool, 1, 1> auto_pause;
        BitField<uint8_t, bool, 2, 1> report;
        BitField<uint8_t, bool, 3, 1> xa_filter;
        BitField<uint8_t, bool, 4, 1> ignore;
        BitField<uint8_t, SectorSize, 5, 1> sector_size;
        BitField<uint8_t, bool, 6, 1> xa_adpcm;
        BitField<uint8_t, DriveSpeed, 7, 1> drive_speed;
    } m_mode;

    enum class Command : uint8_t {
        Sync,
        GetStat,
        SetLoc,
        ReadN = 0x6,
        Pause = 0x9,
        Init,
        Demute = 0xc,
        SetMode = 0xe,
        GetTn = 0x13,
        SeekL = 0x15,
        Test = 0x19,
        GetId = 0x1a,
    };

    struct Timecode {
        uint8_t minute, second, sector;
    } m_setloc_timecode, m_drive_timecode;

    bool m_setloc_unprocessed;

    Command m_command;
    size_t m_command_counter;

    Command m_command2;
    size_t m_command2_counter;

    size_t m_sector_counter;

    size_t m_parameter_fifo_size;
    std::array<uint8_t, ParameterFifoSize> m_parameter_fifo;

    size_t m_response_fifo_size;
    std::array<uint8_t, ResponseFifoSize> m_response_fifo;

    size_t m_data_fifo_size;
    std::array<uint8_t, DiscSectorSize> m_data_fifo;

    std::array<uint8_t, DiscSectorSize> m_sector_buffer;

    uint8_t m_interrupt_enables, m_interrupt_flags;

    std::unique_ptr<Disc> m_disc;
    Emulator *m_emulator;
};

}

#endif /* CORE_CDC_HPP */
