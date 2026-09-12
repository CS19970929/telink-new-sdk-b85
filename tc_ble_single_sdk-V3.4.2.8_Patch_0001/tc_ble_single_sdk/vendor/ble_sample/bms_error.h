#ifndef BMS_ERROR_H_
#define BMS_ERROR_H_

#include <stdint.h>

/*
 * BMS system error flags.
 *
 * Keep the legacy structure name and field layout while moving ownership out
 * of app.h.  New code should include bms_error.h directly.
 */
struct SYSTEM_ERROR
{
    uint8_t u8ErrFlag_Com_AFE1;
    uint8_t u8ErrFlag_Com_AFE2;
    uint8_t u8ErrFlag_Com_Can;
    uint8_t u8ErrFlag_Com_EEPROM;

    uint8_t u8ErrFlag_Com_SPI;
    uint8_t u8ErrFlag_Com_Upper;
    uint8_t u8ErrFlag_Com_Client;
    uint8_t u8ErrFlag_Com_Screen;

    uint8_t u8ErrFlag_Com_Wifi;
    uint8_t u8ErrFlag_Com_BlueTooth;
    uint8_t u8ErrFlag_Com_App;
    uint8_t u8ErrFlag_CBC_CHG;

    uint8_t u8ErrFlag_Store_EEPROM;
    uint8_t u8ErrFlag_HSE;
    uint8_t u8ErrFlag_LSE;
    uint8_t u8ErrFlag_Vdelta_OVER;

    uint8_t u8ErrFlag_Balanced;
    uint8_t u8ErrFlag_ADC;
    uint8_t u8ErrFlag_Heat;
    uint8_t u8ErrFlag_Cool;

    uint8_t u8ErrFlag_CBC_DSG;
    uint8_t u8ErrFlag_SOC_Cail;
    uint8_t u8ErrFlag_TempBreak;
    uint8_t u8ErrFlag_DsgShort;
};

extern volatile struct SYSTEM_ERROR System_ErrFlag;

#endif /* BMS_ERROR_H_ */
