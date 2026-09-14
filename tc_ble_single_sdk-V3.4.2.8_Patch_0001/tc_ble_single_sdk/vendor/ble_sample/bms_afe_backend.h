#ifndef BMS_AFE_BACKEND_H_
#define BMS_AFE_BACKEND_H_

/* Compile-time AFE backend identifiers shared by product branches. */
#define BMS_AFE_BACKEND_DVC1124     1
#define BMS_AFE_BACKEND_SH3673510   2

/* HS-D008 production profile: TLSR8251 + DVC1124-2. */
#ifndef BMS_AFE_BACKEND
#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124
#endif

#endif /* BMS_AFE_BACKEND_H_ */
