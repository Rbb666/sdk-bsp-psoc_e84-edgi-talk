#include "pal_mame_opl2_static.h"
#include "third_party/adplug/rix.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

class TestOpl final : public Copl
{
public:
    TestOpl() : Copl(TYPE_OPL2) {}

    void init() override
    {
        PalMameOpl2_Reset();
    }

    void write(int reg, int value) override
    {
        PalMameOpl2_Write(static_cast<uint8_t>(reg),
                          static_cast<uint8_t>(value));
    }

    void update(short *samples, int frames) override
    {
        PalMameOpl2_Render(reinterpret_cast<int16_t *>(samples),
                           static_cast<size_t>(frames));
    }

    bool getstereo() override
    {
        return false;
    }
};

static void program_voice()
{
    PalMameOpl2_Write(0x01u, 0x20u);
    PalMameOpl2_Write(0x20u, 0x21u);
    PalMameOpl2_Write(0x23u, 0x01u);
    PalMameOpl2_Write(0x40u, 0x28u);
    PalMameOpl2_Write(0x43u, 0x00u);
    PalMameOpl2_Write(0x60u, 0xf3u);
    PalMameOpl2_Write(0x63u, 0xf2u);
    PalMameOpl2_Write(0x80u, 0x55u);
    PalMameOpl2_Write(0x83u, 0x34u);
    PalMameOpl2_Write(0xc0u, 0x04u);
    PalMameOpl2_Write(0xa0u, 0x98u);
    PalMameOpl2_Write(0xb0u, 0x31u);
}

static void test_rix_validation()
{
    uint8_t valid[16] = {0};
    uint8_t invalid[16] = {0};
    TestOpl opl;
    CrixPlayer decoder(&opl);

    valid[0] = 0xaau;
    valid[1] = 0x55u;
    valid[8] = 2u;
    valid[12] = 14u;
    valid[15] = 0x80u;

    assert(!decoder.load_buffer(invalid, sizeof(invalid)));
    assert(decoder.load_buffer(valid, sizeof(valid)));
    assert(!decoder.update());
}

static void test_opl_output_and_reset_determinism()
{
    int16_t first[315] = {0};
    int16_t second[315] = {0};
    size_t nonzero = 0u;

    PalMameOpl2_Init();
    program_voice();
    PalMameOpl2_Render(first, 315u);
    PalMameOpl2_Reset();
    program_voice();
    PalMameOpl2_Render(second, 315u);

    assert(std::memcmp(first, second, sizeof(first)) == 0);
    for (size_t i = 0u; i < 315u; ++i)
    {
        nonzero += first[i] != 0;
    }
    assert(nonzero != 0u);
    assert(PalMameOpl2_StateBytes() > 0u);
    assert(PalMameOpl2_StateBytes() < 8192u);
    assert(PalMameOpl2_TableBytes() == 25706u);
}

int main()
{
    test_rix_validation();
    test_opl_output_and_reset_determinism();
    std::puts("rix_output: PASS");
    return 0;
}
