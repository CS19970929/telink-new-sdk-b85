#include "safety_manager.h"

int Safety_MosRuntimeCheck(void)
{
#if SAFETY_ENABLE
    /* 当前板级没有独立 MOS 反馈输入，先保留接口，后续接入反馈脚后启用一致性判断。 */
    return 1;
#else
    return 1;
#endif
}
