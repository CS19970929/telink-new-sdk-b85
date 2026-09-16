#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static struct {uint16_t u16Ichg,u16IDischg;}g_stCellInfoReport;
/* CURRENT_FLOOR */
/* PRODUCTION_SOURCE */
int main(void){
 const int32_t inputs[]={INT32_MIN,-201,-200,-199,-1,0,1,199,200,201,499,500,INT32_MAX};
 for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);i++){
  int32_t ma=inputs[i];uint32_t magnitude=ma<0?0u-(uint32_t)ma:(uint32_t)ma;
  uint32_t expected=magnitude<=200?0:magnitude/100;
  if(expected>65535)expected=65535;
  g_stCellInfoReport.u16Ichg=g_stCellInfoReport.u16IDischg=99;
  dvc_publish_current_report(ma);
  assert(g_stCellInfoReport.u16Ichg==(ma<0?expected:0));
  assert(g_stCellInfoReport.u16IDischg==(ma>=0?expected:0));
 }
 puts("PASS current: +/-199/200 masked, +/-201 reported, both directions and saturation");
}
