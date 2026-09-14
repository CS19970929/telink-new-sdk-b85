#ifndef D008_PRODUCT_PROFILE_H_
#define D008_PRODUCT_PROFILE_H_

/*
 * HS-D008 assembly profile boundary.
 *
 * The schematic identifies two product assemblies: 24S LFP and 20S NMC.
 * This header keeps physical assembly identity separate from the DVC1124
 * register driver. Product protection/SOC values remain owned by their
 * existing parameter/profile layers and must not be inferred from cell count.
 */
#define D008_PRODUCT_PROFILE_24S_LFP  1u
#define D008_PRODUCT_PROFILE_20S_NMC  2u

#ifndef D008_PRODUCT_PROFILE
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
#endif

#if (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_24S_LFP)
#define D008_PROFILE_CELL_COUNT      24u
#define D008_PROFILE_CHEMISTRY_LFP   1u
#define D008_PROFILE_CHEMISTRY_NMC   0u
#elif (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_20S_NMC)
#define D008_PROFILE_CELL_COUNT      20u
#define D008_PROFILE_CHEMISTRY_LFP   0u
#define D008_PROFILE_CHEMISTRY_NMC   1u
#else
#error "Unsupported HS-D008 product profile"
#endif

#endif /* D008_PRODUCT_PROFILE_H_ */
