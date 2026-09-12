#ifndef SCI_H
#define SCI_H

/*
 * Legacy compatibility header.
 *
 * The BMS runtime data model is transport- and AFE-independent and now lives
 * in bms_state.h. Existing modules may continue including sci_upper.h during
 * the staged migration; new code should include bms_state.h directly.
 */
#include "bms_state.h"

#endif /* SCI_H */
