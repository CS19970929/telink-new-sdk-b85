#ifndef BMS_AFE_BACKEND_H_
#define BMS_AFE_BACKEND_H_

/*
 * Compile-time AFE backend selection.
 *
 * HS-D008 remains a DVC1124 product. The SAFE backend adds the D013-style
 * supervisory/arbitration wrapper without changing the DVC1124 register model,
 * I2C protocol, board GPIOs, Flash layout, OTA boundary, or application API.
 */
#define BMS_AFE_BACKEND_DVC1124_LEGACY  1
#define BMS_AFE_BACKEND_DVC1124_SAFE    2

#ifndef BMS_AFE_BACKEND
#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124_SAFE
#endif

#endif /* BMS_AFE_BACKEND_H_ */
