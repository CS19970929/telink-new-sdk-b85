#ifndef DVC1124_PROJECT_CONFIG_H_
#define DVC1124_PROJECT_CONFIG_H_

/*
 * HS-D008 DVC1124 project identity.
 *
 * Address values use Telink's 8-bit transfer-address convention (write address,
 * R/W bit at bit0). Change these definitions per board variant; no driver code
 * needs to be edited.
 */
#define DVC1124_DEFAULT_MODEL                DVC1124_MODEL_22
#define DVC1124_DEFAULT_ADDR_MODE            DVC1124_ADDR_FIXED
#define DVC1124_DEFAULT_HARDWIRE_CODE        0u
#define DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR  0x40u

/* HS-D008 default assembly: 24S. Set to 20 for the documented 20S variant. */
#define DVC1124_DEFAULT_CELL_COUNT           24u

/* Ten 2 mOhm shunts in parallel on HS-D008: 0.2 mOhm = 200 uOhm. */
#define DVC1124_DEFAULT_SHUNT_UOHM           200u

/* HS-D008 schematic: NTC1 -> GP4, NTC2 -> GP1. */
#define DVC1124_DEFAULT_BATTERY_NTC_GP       4u
#define DVC1124_DEFAULT_MOS_NTC_GP           1u

/*
 * Product short-circuit threshold/delay were not supplied in the project
 * parameter set. Keep DVC SCD disabled instead of guessing a production value.
 */
#define DVC1124_HW_SCD_THRESHOLD_MV          0u
#define DVC1124_HW_SCD_DELAY_US              0u

#endif /* DVC1124_PROJECT_CONFIG_H_ */
