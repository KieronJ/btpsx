#ifndef CORE_DMAC_HPP
#define CORE_DMAC_HPP

#include <cstdint>

#include <common/bitfield.hpp>

namespace Core
{

class Emulator;

class Dmac {
public:
    Dmac(Emulator *emulator);

    void Reset();

    uint32_t Read(uint32_t addr);
    void Write(uint32_t addr, uint32_t data);

private:
    enum Channel { MdecIn, MdecOut, Gpu, Cdrom, Spu, Pio, Otc, Count };

    void StartTransferMdecIn();
    void StartTransferGpu();
    void StartTransferCdrom();
    void StartTransferSpu();
    void StartTransferOtc();

    void UpdateInterrupts();

    enum Direction : uint32_t { ToRam, FromRam };
    enum SyncMode : uint32_t { Manual, Block, LinkedList, Reserved };

    struct DmaChannel {
        union {
            uint32_t raw;

            BitField<uint32_t, uint32_t, 0, 24> address;
        } madr;

        union {
            uint32_t raw;
 
            BitField<uint32_t, uint32_t, 0, 16> size;
            BitField<uint32_t, uint32_t, 16, 16> count;
        } bcr;

        union {
            uint32_t raw;

            BitField<uint32_t, Direction, 0, 1> direction;
            BitField<uint32_t, bool, 1, 1> backward;
            BitField<uint32_t, SyncMode, 9, 2> sync_mode;
            BitField<uint32_t, bool, 24, 1> enable;
            BitField<uint32_t, bool, 28, 1> start;
        } chcr;
    } m_channels[Channel::Count];

    uint32_t m_dpcr;

    union {
        uint32_t raw;
 
        BitField<uint32_t, bool, 15, 1> force;
        BitField<uint32_t, uint32_t, 16, 7> enable;
        BitField<uint32_t, bool, 23, 1> master;
        BitField<uint32_t, uint32_t, 24, 7> flag;
        BitField<uint32_t, bool, 31, 1> irq;
    } m_dicr;

    Emulator *m_emulator;
};

}

#endif /* CORE_DMAC_HPP */
