#include "board.h"
#include "nerdqaxeplus2.h"

static const char* TAG="nerdqaxeplus2";

NerdQaxePlus2::NerdQaxePlus2() : NerdQaxePlus() {
    m_deviceModel = "NerdQAxe++";
    m_miningAgent = m_deviceModel;
    m_asicModel = "BM1370";
    m_asicCount = 4;
    m_numPhases = 3;
    m_imax = m_numPhases * 30;
    m_ifault = (float) (m_imax + 5);

    m_asicJobIntervalMs = 500;
    // full selectable range for live tuning in the web UI (ceiling 800 MHz / 1400 mV)
    m_asicFrequencies = {500, 525, 550, 575, 600, 625, 650, 675, 700, 725, 750, 775, 800};
    m_asicVoltages = {1100, 1120, 1150, 1180, 1200, 1220, 1250, 1280, 1300, 1350, 1400};
    m_defaultAsicFrequency = m_asicFrequency = 600;          // safe boot default
    m_defaultAsicVoltageMillis = m_asicVoltageMillis = 1150; // safe boot default
    m_absMaxAsicFrequency = 800;
    m_absMaxAsicVoltageMillis = 1400;
    m_initVoltageMillis = 1200;

    m_pidSettings[0].targetTemp = 55;
    m_pidSettings[0].p = 600;  //  6.00
    m_pidSettings[0].i = 10;   //  0.10
    m_pidSettings[0].d = 1000; // 10.00

    m_maxPin = 100.0;
    m_minPin = 52.0;
    m_maxVin = 13.0;
    m_minVin = 11.0;
    m_minCurrentA = 0.0f;
    m_maxCurrentA = 8.0f;

    m_asicMaxDifficulty = 2048;
    m_asicMinDifficulty = 512;
    m_asicMinDifficultyDualPool = 256;

#ifdef NERDQAXEPLUS2
    // big-screen variant: Mehrunes Dagon theme (480x320). replaces the stock
    // ThemeNerdqaxeplus2 so the unused themes get linker-GC'd out of the binary.
    m_theme = new ThemeDagon();
#endif
    m_asics = new BM1370();
    m_hasHashCounter = true;
    m_vrFrequency = m_defaultVrFrequency = m_asics->getDefaultVrFrequency();
}

float NerdQaxePlus2::getTemperature(int index) {
    float temp = NerdQaxePlus::getTemperature(index);
    if (!temp) {
        return 0.0;
    }
    // we can't read the real chip temps but this should be about right
    return temp + 10.0f; // offset of 10°C
}

void NerdQaxePlus2::requestChipTemps() {
    // NOP
}
