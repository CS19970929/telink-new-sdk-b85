# DVC1124-2参考手册_V1.2.pdf 逐页原文提取

用途：检索支持层，非规范化说明。PDF 为最终证据。表格列、上下标、公式、图中连线及希腊字母可能在文本提取中失真。PDF 页从 1 起计；打印页码请直接核对原页。

## PDF 第 1 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.1](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=1)

```text
4 串至 24 串电池组监控芯片                                                                                                     DVC1124-2 参考手册
寄存器 ...................................................................... 2             充/放电控制寄存器 ............................... 15
  寄存器列表 ...................................................... 2                         放电屏蔽控制寄存器 ............................ 16
  寄存器描述 ...................................................... 6                         充电屏蔽控制寄存器 ............................ 17
       警报标识寄存器 ...................................... 6                                  CADC 控制寄存器 .................................. 17
       芯片状态寄存器 ...................................... 6                                  1 级放电过流保护阈值寄存器 ............. 18
       CC1 寄存器 ............................................... 7                         1 级充电过流保护阈值寄存器 ............. 19
       CC2 寄存器 ............................................... 7                         1 级放电过流保护延迟寄存器 ............. 19
       VTOP 寄存器 ............................................ 8                           1 级充电过流保护延迟寄存器 ............. 19
       VPK 寄存器 ............................................... 8                         2 级放电过流保护阈值寄存器 ............. 19
       VLD 寄存器 ............................................... 8                         2 级充电过流保护阈值寄存器 ............. 19
       VCT 寄存器 ............................................... 8                         2 级放电过流保护延迟寄存器 ............. 20
       V1P8 寄存器 ............................................. 9                          2 级充电过流保护延迟寄存器 ............. 20
       VGP1 寄存器 ............................................ 9                           放电短路保护阈值寄存器 .................... 20
       VGP2 寄存器 ............................................ 9                           放电短路保护延迟寄存器 .................... 20
       VGP3 寄存器 ............................................ 9                           电流唤醒寄存器 .................................... 20
       VGP4 寄存器 ............................................ 9                           续流保护寄存器 .................................... 21
       VGP5 寄存器 .......................................... 10                            电池被动均衡寄存器 ............................ 21
       VGP6 寄存器 .......................................... 10                            电池屏蔽寄存器 .................................... 21
       VC1 寄存器 ............................................. 10                          电荷泵控制寄存器 ................................ 22
       VC2 寄存器 ............................................. 10                          VADC 控制寄存器 .................................. 22
       VC3 寄存器 ............................................. 10                          电池过压保护寄存器 ............................ 23
       VC4 寄存器 ............................................. 11                          电压欠压保护寄存器 ............................ 24
       VC5 寄存器 ............................................. 11                          GP1，GP2 和 GP3 模式寄存器.............. 24
       VC6 寄存器 ............................................. 11                          GP4，GP5 和 GP6 模式寄存器.............. 25
       VC7 寄存器 ............................................. 11                          芯片过温保护寄存器 ............................ 26
       VC8 寄存器 ............................................. 11                          I2C 看门狗寄存器 ................................... 26
       VC9 寄存器 ............................................. 12                          定时唤醒寄存器 .................................... 26
       VC10 寄存器 ........................................... 12                           中断屏蔽寄存器 .................................... 27
       VC11 寄存器 ........................................... 12                           电阻修调寄存器 .................................... 28
       VC12 寄存器 ........................................... 12                           芯片版本寄存器 .................................... 29
       VC13 寄存器 ........................................... 12                       管理系统 ................................................................ 30
       VC14 寄存器 ........................................... 13                         放电管理 ........................................................ 30
       VC15 寄存器 ........................................... 13                           放电驱动逻辑 ........................................ 30
       VC16 寄存器 ........................................... 13                           预放电驱动逻辑 .................................... 30
       VC17 寄存器 ........................................... 13                         充电管理 ........................................................ 31
       VC18 寄存器 ........................................... 13                           充电驱动逻辑 ........................................ 31
       VC19 寄存器 ........................................... 14                           预充电驱动逻辑 .................................... 31
       VC20 寄存器 ........................................... 14                         逻辑符号说明 ................................................ 31
       VC21 寄存器 ........................................... 14                       应用和实施 ............................................................ 32
       VC22 寄存器 ........................................... 14                         电池电压二次校准 ........................................ 32
       VC23 寄存器 ........................................... 14                       修订历史记录 ........................................................ 33
       VC24 寄存器 ........................................... 15
       VVOS 寄存器 .......................................... 15
       CVOS 寄存器 .......................................... 15

南京集澈电子科技有限公司                                                                   1.2                                                                 1 / 33
宜矽源半导体南京有限公司
```

## PDF 第 2 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.2](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=2)

```text
DVC1124-2 参考手册                                                   4 串至 24 串电池组监控芯片

寄存器

寄存器列表
 OFFSET    BIT 7      BIT 6      BIT 5      BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
  0x00     IWTF        COV        CUV       OCD1       OCC1         OCD2       OCC2      SCD
  0x01      PD        VADF       CC1F       CC2F       CST[3]      CST[2]      CST[1]   CST[0]
  0x02    CC1[15]    CC1[14]    CC1[13]    CC1[12]    CC1[11]      CC1[10]     CC1[9]   CC1[8]
  0x03     CC1[7]     CC1[6]     CC1[5]     CC1[4]     CC1[3]      CC1[2]      CC1[1]   CC1[0]
  0x04    CC2[19]    CC2[18]    CC2[17]    CC2[16]    CC2[15]      CC2[14]    CC2[13]   CC2[12]
  0x05    CC2[11]    CC2[10]     CC2[9]     CC2[8]     CC2[7]      CC2[6]      CC2[5]   CC2[4]
  0x06     CC2[3]     CC2[2]     CC2[1]     CC2[0]     PDSGF       PCHGF       DSGF      CHGF
  0x07    VTP[15]    VTP[14]    VTP[13]    VTP[12]    VTP[11]      VTP[10]     VTP[9]   VTP[8]
  0x08     VTP[7]     VTP[6]     VTP[5]     VTP[4]     VTP[3]      VTP[2]      VTP[1]   VTP[0]
  0x09    VPK[15]    VPK[14]    VPK[13]    VPK[12]    VPK[11]      VPK[10]     VPK[9]   VPK[8]
  0x0A     VPK[7]     VPK[6]     VPK[5]     VPK[4]     VPK[3]      VPK[2]      VPK[1]   VPK[0]
  0x0B    VLD[15]    VLD[14]    VLD[13]    VLD[12]    VLD[11]      VLD[10]     VLD[9]   VLD[8]
  0x0C     VLD[7]     VLD[6]     VLD[5]     VLD[4]     VLD[3]      VLD[2]      VLD[1]   VLD[0]
  0x0D    VCT[15]    VCT[14]    VCT[13]    VCT[12]    VCT[11]      VCT[10]     VCT[9]   VCT[8]
  0x0E     VCT[7]     VCT[6]     VCT[5]     VCT[4]     VCT[3]      VCT[2]      VCT[1]   VCT[0]
  0x0F    V1P8[15]   V1P8[14]   V1P8[13]   V1P8[12]   V1P8[11]    V1P8[10]    V1P8[9]   V1P8[8]
  0x10    V1P8[7]    V1P8[6]    V1P8[5]    V1P8[4]    V1P8[3]      V1P8[2]    V1P8[1]   V1P8[0]
  0x11    VGP1[15]   VGP1[14]   VGP1[13]   VGP1[12]   VGP1[11]    VGP1[10]    VGP1[9]   VGP1[8]
  0x12    VGP1[7]    VGP1[6]    VGP1[5]    VGP1[4]    VGP1[3]      VGP1[2]    VGP1[1]   VGP1[0]
  0x13    VGP2[15]   VGP2[14]   VGP2[13]   VGP2[12]   VGP2[11]    VGP2[10]    VGP2[9]   VGP2[8]
  0x14    VGP2[7]    VGP2[6]    VGP2[5]    VGP2[4]    VGP2[3]      VGP2[2]    VGP2[1]   VGP2[0]
  0x15    VGP3[15]   VGP3[14]   VGP3[13]   VGP3[12]   VGP3[11]    VGP3[10]    VGP3[9]   VGP3[8]
  0x16    VGP3[7]    VGP3[6]    VGP3[5]    VGP3[4]    VGP3[3]      VGP3[2]    VGP3[1]   VGP3[0]
  0x17    VGP4[15]   VGP4[14]   VGP4[13]   VGP4[12]   VGP4[11]    VGP4[10]    VGP4[9]   VGP4[8]
  0x18    VGP4[7]    VGP4[6]    VGP4[5]    VGP4[4]    VGP4[3]      VGP4[2]    VGP4[1]   VGP4[0]
  0x19    VGP5[15]   VGP5[14]   VGP5[13]   VGP5[12]   VGP5[11]    VGP5[10]    VGP5[9]   VGP5[8]
  0x1A    VGP5[7]    VGP5[6]    VGP5[5]    VGP5[4]    VGP5[3]      VGP5[2]    VGP5[1]   VGP5[0]
  0x1B    VGP6[15]   VGP6[14]   VGP6[13]   VGP6[12]   VGP6[11]    VGP6[10]    VGP6[9]   VGP6[8]
  0x1C    VGP6[7]    VGP6[6]    VGP6[5]    VGP6[4]    VGP6[3]      VGP6[2]    VGP6[1]   VGP6[0]
  0x1D    VC1[15]    VC1[14]    VC1[13]    VC1[12]    VC1[11]      VC1[10]     VC1[9]   VC1[8]
  0x1E     VC1[7]     VC1[6]     VC1[5]     VC1[4]     VC1[3]      VC1[2]      VC1[1]   VC1[0]
  0x1F    VC2[15]    VC2[14]    VC2[13]    VC2[12]    VC2[11]      VC2[10]     VC2[9]   VC2[8]
  0x20     VC2[7]     VC2[6]     VC2[5]     VC2[4]     VC2[3]      VC2[2]      VC2[1]   VC2[0]
  0x21    VC3[15]    VC3[14]    VC3[13]    VC3[12]    VC3[11]      VC3[10]     VC3[9]   VC3[8]
  0x22     VC3[7]     VC3[6]     VC3[5]     VC3[4]     VC3[3]      VC3[2]      VC3[1]   VC3[0]
  0x23    VC4[15]    VC4[14]    VC4[13]    VC4[12]    VC4[11]      VC4[10]     VC4[9]   VC4[8]
  0x24     VC4[7]     VC4[6]     VC4[5]     VC4[4]     VC4[3]      VC4[2]      VC4[1]   VC4[0]
  0x25    VC5[15]    VC5[14]    VC5[13]    VC5[12]    VC5[11]      VC5[10]     VC5[9]   VC5[8]


2 / 33                                       1.2                            南京集澈电子科技有限公司
                                                                            宜矽源半导体南京有限公司
```

## PDF 第 3 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.3](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=3)

```text
4 串至 24 串电池组监控芯片                                                           DVC1124-2 参考手册
 OFFSET    BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2        BIT 1     BIT 0
  0x26     VC5[7]     VC5[6]     VC5[5]     VC5[4]     VC5[3]     VC5[2]      VC5[1]    VC5[0]
  0x27    VC6[15]    VC6[14]    VC6[13]    VC6[12]    VC6[11]    VC6[10]      VC6[9]    VC6[8]
  0x28     VC6[7]     VC6[6]     VC6[5]     VC6[4]     VC6[3]     VC6[2]      VC6[1]    VC6[0]
  0x29    VC7[15]    VC7[14]    VC7[13]    VC7[12]    VC7[11]    VC7[10]      VC7[9]    VC7[8]
  0x2A     VC7[7]     VC7[6]     VC7[5]     VC7[4]     VC7[3]     VC7[2]      VC7[1]    VC7[0]
  0x2B    VC8[15]    VC8[14]    VC8[13]    VC8[12]    VC8[11]    VC8[10]      VC8[9]    VC8[8]
  0x2C     VC8[7]     VC8[6]     VC8[5]     VC8[4]     VC8[3]     VC8[2]      VC8[1]    VC8[0]
 0x2D     VC9[15]    VC9[14]    VC9[13]    VC9[12]    VC9[11]    VC9[10]      VC9[9]    VC9[8]
  0x2E     VC9[7]     VC9[6]     VC9[5]     VC9[4]     VC9[3]     VC9[2]      VC9[1]    VC9[0]
  0x2F    VC10[15]   VC10[14]   VC10[13]   VC10[12]   VC10[11]   VC10[10]     VC10[9]   VC10[8]
  0x30    VC10[7]    VC10[6]    VC10[5]    VC10[4]    VC10[3]    VC10[2]      VC10[1]   VC10[0]
  0x31    VC11[15]   VC11[14]   VC11[13]   VC11[12]   VC11[11]   VC11[10]     VC11[9]   VC11[8]
  0x32    VC11[7]    VC11[6]    VC11[5]    VC11[4]    VC11[3]    VC11[2]      VC11[1]   VC11[0]
  0x33    VC12[15]   VC12[14]   VC12[13]   VC12[12]   VC12[11]   VC12[10]     VC12[9]   VC12[8]
  0x34    VC12[7]    VC12[6]    VC12[5]    VC12[4]    VC12[3]    VC12[2]      VC12[1]   VC12[0]
  0x35    VC13[15]   VC13[14]   VC13[13]   VC13[12]   VC13[11]   VC13[10]     VC13[9]   VC13[8]
  0x36    VC13[7]    VC13[6]    VC13[5]    VC13[4]    VC13[3]    VC13[2]      VC13[1]   VC13[0]
  0x37    VC14[15]   VC14[14]   VC14[13]   VC14[12]   VC14[11]   VC14[10]     VC14[9]   VC14[8]
  0x38    VC14[7]    VC14[6]    VC14[5]    VC14[4]    VC14[3]    VC14[2]      VC14[1]   VC14[0]
  0x39    VC15[15]   VC15[14]   VC15[13]   VC15[12]   VC15[11]   VC15[10]     VC15[9]   VC15[8]
  0x3A    VC15[7]    VC15[6]    VC15[5]    VC15[4]    VC15[3]    VC15[2]      VC15[1]   VC15[0]
  0x3B    VC16[15]   VC16[14]   VC16[13]   VC16[12]   VC16[11]   VC16[10]     VC16[9]   VC16[8]
  0x3C    VC16[7]    VC16[6]    VC16[5]    VC16[4]    VC16[3]    VC16[2]      VC16[1]   VC16[0]
 0x3D     VC17[15]   VC17[14]   VC17[13]   VC17[12]   VC17[11]   VC17[10]     VC17[9]   VC17[8]
  0x3E    VC17[7]    VC17[6]    VC17[5]    VC17[4]    VC17[3]    VC17[2]      VC17[1]   VC17[0]
  0x3F    VC18[15]   VC18[14]   VC18[13]   VC18[12]   VC18[11]   VC18[10]     VC18[9]   VC18[8]
  0x40    VC18[7]    VC18[6]    VC18[5]    VC18[4]    VC18[3]    VC18[2]      VC18[1]   VC18[0]
  0x41    VC19[15]   VC19[14]   VC19[13]   VC19[12]   VC19[11]   VC19[10]     VC19[9]   VC19[8]
  0x42    VC19[7]    VC19[6]    VC19[5]    VC19[4]    VC19[3]    VC19[2]      VC19[1]   VC19[0]
  0x43    VC20[15]   VC20[14]   VC20[13]   VC20[12]   VC20[11]   VC20[10]     VC20[9]   VC20[8]
  0x44    VC20[7]    VC20[6]    VC20[5]    VC20[4]    VC20[3]    VC20[2]      VC20[1]   VC20[0]
  0x45    VC21[15]   VC21[14]   VC21[13]   VC21[12]   VC21[11]   VC21[10]     VC21[9]   VC21[8]
  0x46    VC21[7]    VC21[6]    VC21[5]    VC21[4]    VC21[3]    VC21[2]      VC21[1]   VC21[0]
  0x47    VC22[15]   VC22[14]   VC22[13]   VC22[12]   VC22[11]   VC22[10]     VC22[9]   VC22[8]
  0x48    VC22[7]    VC22[6]    VC22[5]    VC22[4]    VC22[3]    VC22[2]      VC22[1]   VC22[0]
  0x49    VC23[15]   VC23[14]   VC23[13]   VC23[12]   VC23[11]   VC23[10]     VC23[9]   VC23[8]
  0x4A    VC23[7]    VC23[6]    VC23[5]    VC23[4]    VC23[3]    VC23[2]      VC23[1]   VC23[0]
  0x4B    VC24[15]   VC24[14]   VC24[13]   VC24[12]   VC24[11]   VC24[10]     VC24[9]   VC24[8]
  0x4C    VC24[7]    VC24[6]    VC24[5]    VC24[4]    VC24[3]    VC24[2]      VC24[1]   VC24[0]
 0x4D     VVOS[15]   VVOS[14]   VVOS[13]   VVOS[12]   VVOS[11]   VVOS[10]     VVOS[9]   VVOS[8]
  0x4E    VVOS[7]    VVOS[6]    VVOS[5]    VVOS[4]    VVOS[3]    VVOS[2]      VVOS[1]   VVOS[0]
  0x4F    CVOS[15]   CVOS[14]   CVOS[13]   CVOS[12]   CVOS[11]   CVOS[10]     CVOS[9]   CVOS[8]

南京集澈电子科技有限公司                                 1.2                                          3 / 33
宜矽源半导体南京有限公司
```

## PDF 第 4 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.4](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=4)

```text
DVC1124-2 参考手册                                                   4 串至 24 串电池组监控芯片
 OFFSET    BIT 7      BIT 6      BIT 5      BIT 4      BIT 3        BIT 2      BIT 1      BIT 0
  0x50    CVOS[7]    CVOS[6]    CVOS[5]    CVOS[4]    CVOS[3]      CVOS[2]    CVOS[1]    CVOS[0]
  0x51     LDPU       PDSGC      PCHGC      DSGM      DSGC[1]      DSGC[0]    CHGC[1]    CHGC[0]
  0x52     PDWM                             DPC[4]     DPC[3]      DPC[2]      DPC[1]     DPC[0]
  0x53     PDDM       PCWM       PCCM       PCDM       DWM          DDM        DPDM       DBDM
  0x54     CWM        CO1M       CO2M        CSM        CDM         CCM        CPCM       CBDM
  0x55     HSFM                                        CAEW         CAES                  CAMZ
  0x56                                                C1OW[1]     C1OW[0]     C1OS[1]    C1OS[0]
  0x57
  0x58
  0x59    OCD1T[7]   OCD1T[6]   OCD1T[5    OCD1T[4]   OCD1T[3]    OCD1T[2]    OCD1T[1]   OCD1T[0]
  0x5A    OCC1T[7]   OCC1T[6]   OCC1T[5]   OCC1T[4]   OCC1T[3]    OCC1T[2]    OCC1T[1]   OCC1T[0]
  0x5B    OCD1D[7]   OCD1D[6]   OCD1D[5]   OCD1D[4]   OCD1D[3]    OCD1D[2]    OCD1D[1]   OCD1D[0]
  0x5C    OCCD[7]    OCCD[6]    OCCD[5]    OCCD[4]    OCCD[3]      OCCD[2]    OCCD[1]    OCCD[0]
  0x5D
  0x5E                OCD2E     OCD2T[5]   OCD2T[4]   OCD2T[3]    OCD2T[2]    OCD2T[1]   OCD2T[0]
  0x5F                OCC2E     OCC2T[5]   OCC2T[4]   OCC2T[3]    OCC2T[2]    OCC2T[1]   OCC2T[0]
  0x60    OCD2D[7]   OCD2D[6]   OCD2D[5]   OCD2D[4]   OCD2D[3]    OCD2D[2]    OCD2D[1]   OCD2D[0]
  0x61    OCC2D[7]   OCC2D[6]   OCC2D[5]   OCC2D[4]   OCC2D[3]    OCC2D[2]    OCC2D[1]   OCC2D[0]
  0x62                SCDE      SCDT[5]    SCDT[4]    SCDT[3]      SCDT[2]    SCDT[1]    SCDT[0]
  0x63    SCDD[7]    SCDD[6]    SCDD[5]    SCDD[4]    SCDD[3]      SCDD[2]    SCDD[1]    SCDD[0]
  0x64
  0x65     CWT[7]     CWT[6]     CWT[5]     CWT[4]     CWT[3]      CWT[2]      CWT[1]     CWT[0]
  0x66    BDPT[7]    BDPT[6]    BDPT[5]    BDPT[4]    BDPT[3]      BDPT[2]    BDPT[1]    BDPT[0]
  0x67     CB[24]     CB[23]     CB[22]     CB[21]     CB[20]      CB[19]      CB[18]     CB[17]
  0x68     CB[16]     CB[15]     CB[14]     CB[13]     CB[12]      CB[11]      CB[10]     CB[9]
  0x69     CB[8]      CB[7]      CB[6]      CB[5]      CB[4]        CB[3]      CB[2]      CB[1]
  0x6A     CM[24]     CM[23]     CM[22]     CM[21]     CM[20]      CM[19]      CM[18]     CM[17]
  0x6B     CM[16]     CM[15]     CM[14]     CM[13]     CM[12]      CM[11]      CM[10]     CM[9]
  0x6C     CM[8]      CM[7]      CM[6]      CM[5]       PKM         LDM         CTM       V1P8M
  0x6D                          CPVS[2]    CPVS[1]    CPVS[0]       COW        CMM         CVS
  0x6E      VAE       VASM      VAMP[1]    VAMP[0]                             VAO[1]     VAO[0]
  0x6F
  0x70    COVT[11]   COVT[10]   COVT[9]    COVT[8]    COVT[7]      COVT[6]    COVT[5]    COVT[4]
  0x71    COVT[3]    COVT[2]    COVT[1]    COVT[0]    COVD[3]      COVD[2]    COVD[1]    COVD[0]
  0x72    CUVT[11]   CUVT[10]   CUVT[9]    CUVT[8]    CUVT[7]      CUVT[6]    CUVT[5]    CUVT[4]
  0x73    CUVT[3]    CUVT[2]    CUVT[1]    CUVT[0]    CUVD[3]      CUVD[2]    CUVD[1]    CUVD[0]
  0x74    GP1M[1]    GP1M[0]    GP2M[2]    GP2M[1]    GP2M[0]     GP3M[2]     GP3M[1]    GP3M[0]
  0x75    GP4M[1]    GP4M[0]    GP5M[2]    GP5M[1]    GP5M[0]     GP6M[2]     GP6M[1]    GP6M[0]
  0x76     COTF      COTT[6]    COTT[5]    COTT[4]    COTT[3]      COTT[2]    COTT[1]    COTT[0]
  0x77     V3P3ES    V3P3EW      V3P3M                 IWTS        IWT[2]      IWT[1]     IWT[0]
  0x78     TIWK                                       TWSE[3]      TWSE[2]    TWSE[1]    TWSE[0]
  0x79      IWM       IVOM       ICCM       ICOM       ICUM        IOC1M       IOC2M      ISCDM

4 / 33                                       1.2                            南京集澈电子科技有限公司
                                                                            宜矽源半导体南京有限公司
```

## PDF 第 5 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.5](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=5)

```text
4 串至 24 串电池组监控芯片                                               DVC1124-2 参考手册
 OFFSET   BIT 7    BIT 6    BIT 5   BIT 4    BIT 3    BIT 2       BIT 1    BIT 0
  0x7A
  0x7B
  0x7C
 0x7D
  0x7E    FRT[7]   FRT[6]   FRT5]   FRT[4]   FRT[3]   FRT[2]      FRT[1]   FRT[0]
  0x7F
  0x80
  0x81
  0x82
  0x83
  0x84
  0x85
  0x86
  0x87
  0x88
  0x89
  0x8A
  0x8B
  0x8C
 0x8D
  0x8E
  0x8F    CV[7]    CV[6]    CV[5]   CV[4]    CV[3]    CV[2]       CV[1]    CV[0]
  0x90




南京集澈电子科技有限公司                        1.2                                     5 / 33
宜矽源半导体南京有限公司
```

## PDF 第 6 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

```text
DVC1124-2 参考手册                                                          4 串至 24 串电池组监控芯片

寄存器描述
警报标识寄存器
     0x00            BIT 7             BIT 6   BIT 5   BIT 4   BIT 3      BIT 2      BIT 1    BIT 0
    NAME             IWTF              COV     CUV     OCD1    OCC1       OCD2       OCC2      SCD
    MODE             R/W               R/W     R/W     R/W     R/W        R/W        R/W      R/W
   DEFAULT             0                0       0       0        0          0          0        0
IWTF ··················I2C 看门狗溢出标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生 I2C 看门狗溢出
1·························已发生 I2C 看门狗溢出
COV····················电池过压标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生电池过压
1·························已发生电池过压
CUV····················电池欠压标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生电池欠压
1·························已发生电池欠压
OCD1 ·················1 级放电过流标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生 1 级放电过流
1·························已发生 1 级放电过流
OCC1 ··················1 级充电过流标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生 1 级充电过流
1·························已发生 1 级充电过流
OCD2 ·················2 级放电过流标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生 2 级放电过流
1·························已发生 2 级放电过流
OCC2 ··················2 级充电过流标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生 2 级充电过流
1·························已发生 2 级充电过流
SCD ····················放电短路标识位，将该 bit 置 0 即可清除，置 1 无效
0·························未发生放电短路
1·························已发生放电短路

芯片状态寄存器
     0x01            BIT 7             BIT 6   BIT 5   BIT 4   BIT 3      BIT 2      BIT 1    BIT 0
    NAME              PD               VADF    CC1F    CC2F    CST[3]     CST[2]     CST[1]   CST[0]
    MODE               R                RC      RC      RC     R/W        R/W        R/W      R/W
   DEFAULT             0                0       0       0        0          0          0        0
PD ······················充电器检测标识位
0·························未检测到充电器
1·························已检测到充电器
VADF ··················VADC 转换完成标识位，I2C 读取该地址后会自动清零
0·························VADC 未完成转换
1·························VADC 已完成转换
CC1F···················CADC CC1 转换完成标识位，I2C 读取该地址后会自动清零


 6 / 33                                                1.2                        南京集澈电子科技有限公司
                                                                                  宜矽源半导体南京有限公司
```

## PDF 第 7 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

```text
4 串至 24 串电池组监控芯片                                                                    DVC1124-2 参考手册
0·························CADC CC1 未完成转换
1·························CADC CC1 已完成转换
CC2F···················CADC CC2 转换完成标识位，I2C 读取该地址后会自动清零
0·························CADC CC2 未完成转换
1·························CADC CC2 已完成转换
CST[3:0] ·············芯片状态标识位，写入 1101，1110，1111 有效，写入其他值无效
0000···················芯片从关机状态被唤醒
0001···················芯片从休眠状态被 I2C 通信唤醒
0010···················芯片从休眠状态被定时唤醒
0011···················芯片从休眠状态被放电电流唤醒
0100···················芯片从休眠状态被充电电流唤醒
0101···················芯片从休眠状态被 2 级放电过流唤醒
0110···················芯片从休眠状态被 2 级充电过流唤醒
0111···················芯片从休眠状态被放电短路唤醒
1000···················芯片从休眠状态被充电器唤醒
1001···················N/A
1010···················N/A
1011···················芯片正在等待关机
1100···················芯片正在等待休眠
1101···················使所有寄存器复位为默认值
1110···················使芯片进入休眠状态
1111···················使芯片进入关断状态

CC1 寄存器
      0x02               BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME               CC1[15]    CC1[14]   CC1[13]   CC1[12]   CC1[11]   CC1[10]     CC1[9]    CC1[8]
     MODE                    R        R        R         R         R         R           R         R

      0x03               BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME                CC1[7]    CC1[6]    CC1[5]    CC1[4]    CC1[3]    CC1[2]      CC1[1]    CC1[0]
     MODE                    R        R        R         R         R         R           R         R
CC1[15:0] ···········CADC CC1 电流值，有符号二进制补码，正值为放电，负值为充电，LSB=5μV

CC2 寄存器
      0x04               BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME               CC2[19]    CC2[18]   CC2[17]   CC2[16]   CC2[15]   CC2[14]     CC2[13]   CC2[12]
     MODE                    R        R        R         R         R         R           R         R

      0x05               BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME               CC2[11]    CC2[10]   CC2[9]    CC2[8]    CC2[7]    CC2[6]      CC2[5]    CC2[4]
     MODE                    R        R        R         R         R         R           R         R

      0x06               BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME                CC2[3]    CC2[2]    CC2[1]    CC2[0]    PDSGF     PCHGF        DSGF      CHGF
     MODE                    R        R        R         R         R         R           R         R
CC2[19:0] ···········CADC CC2 电流值，有符号二进制补码，正值为放电，负值为充电，LSB=5/16μV=0.3125μV
PDSGF ················PDSG 驱动输出标识位
0·························PDSG 驱动输出已关闭

南京集澈电子科技有限公司                                            1.2                                        7 / 33
宜矽源半导体南京有限公司
```

## PDF 第 8 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

```text
DVC1124-2 参考手册                                                           4 串至 24 串电池组监控芯片
1·························PDSG 驱动输出已开启
PCHGF················PCHG 驱动输出标识位
0·························PCHG 驱动输出已关闭
1·························PCHG 驱动输出已开启
DSGF ··················DSG 驱动输出标识位
0·························DSG 驱动输出已关闭
1·························DSG 驱动输出已开启
CHGF ··················CHG 驱动输出标识位
0·························CHG 驱动输出已关闭
1·························CHG 驱动输出已开启

VTOP 寄存器
     0x07          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME          VTP[15]        VTP[14]   VTP[13]   VTP[12]   VTP[11]     VTP[10]     VTP[9]   VTP[8]
    MODE             R              R        R         R         R           R           R        R

     0x08          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME           VTP[7]        VTP[6]    VTP[5]    VTP[4]    VTP[3]      VTP[2]      VTP[1]   VTP[0]
    MODE             R              R        R         R         R           R           R        R
VTP[15:0]···········VTOP 电压值，无符号二进制，LSB=12.8mV

VPK 寄存器
     0x09          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME          VPK[15]        VPK[14]   VPK[13]   VPK[12]   VPK[11]     VPK[10]     VPK[9]   VPK[8]
    MODE             R              R        R         R         R           R           R        R

    0x0A           BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME           VPK[7]        VPK[6]    VPK[5]    VPK[4]    VPK[3]      VPK[2]      VPK[1]   VPK[0]
    MODE             R              R        R         R         R           R           R        R
VPK[15:0] ··········PACK 电压值，无符号二进制，LSB=12.8mV

VLD 寄存器
     0x0B          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME          VLD[15]        VLD[14]   VLD[13]   VLD[12]   VLD[11]     VLD[10]     VLD[9]   VLD[8]
    MODE             R              R        R         R         R           R           R        R
     0x0C          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME           VLD[7]        VLD[6]    VLD[5]    VLD[4]    VLD[3]      VLD[2]      VLD[1]   VLD[0]
    MODE             R              R        R         R         R           R           R        R
VLD[15:0] ··········LOAD 电压值，无符号二进制，LSB=12.8mV

VCT 寄存器
    0x0D           BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME          VCT[15]        VCT[14]   VCT[13]   VCT[12]   VCT[11]     VCT[10]     VCT[9]   VCT[8]
    MODE             R              R        R         R         R           R           R        R
     0x0E          BIT 7          BIT 6     BIT 5     BIT 4     BIT 3       BIT 2      BIT 1    BIT 0
    NAME           VCT[7]        VCT[6]    VCT[5]    VCT[4]    VCT[3]      VCT[2]      VCT[1]   VCT[0]
    MODE             R              R        R         R         R           R           R        R


 8 / 33                                               1.2                           南京集澈电子科技有限公司
                                                                                    宜矽源半导体南京有限公司
```

## PDF 第 9 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

```text
4 串至 24 串电池组监控芯片                                                              DVC1124-2 参考手册
VCT[15:0]···········芯片内核温度值，无符号二进制，芯片内核温度=VCT×0.24467°C-271.03°C

V1P8 寄存器
   0x0F        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      V1P8[15]   V1P8[14]    V1P8[13]   V1P8[12]   V1P8[11]   V1P8[10]    V1P8[9]   V1P8[8]
   MODE         R          R           R          R          R          R          R         R

   0x10        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME       V1P8[7]    V1P8[6]    V1P8[5]    V1P8[4]    V1P8[3]    V1P8[2]     V1P8[1]   V1P8[0]
   MODE         R          R           R          R          R          R          R         R
V1P8[15:0]·········V1P8 电压值，无符号二进制，LSB=100μV

VGP1 寄存器
   0x11        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP1[15]   VGP1[14]    VGP1[13]   VGP1[12]   VGP1[11]   VGP1[10]    VGP1[9]   VGP1[8]
   MODE         R          R           R          R          R          R          R         R

   0x12        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP1[7]     VGP1[6]    VGP1[5]    VGP1[4]    VGP1[3]    VGP1[2]     VGP1[1]   VGP1[0]
   MODE         R          R           R          R          R          R          R         R
VGP1[15:0] ········GP1 电压值，无符号二进制，LSB=100μV

VGP2 寄存器
   0x13        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP2[15]   VGP2[14]    VGP2[13]   VGP2[12]   VGP2[11]   VGP2[10]    VGP2[9]   VGP2[8]
   MODE         R          R           R          R          R          R          R         R

   0x14        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP2[7]     VGP2[6]    VGP2[5]    VGP2[4]    VGP2[3]    VGP2[2]     VGP2[1]   VGP2[0]
   MODE         R          R           R          R          R          R          R         R
VGP2[15:0] ········GP2 电压值，无符号二进制，LSB=100μV

VGP3 寄存器
   0x15        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP3[15]   VGP3[14]    VGP3[13]   VGP3[12]   VGP3[11]   VGP3[10]    VGP3[9]   VGP3[8]
   MODE         R          R           R          R          R          R          R         R
   0x16        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP3[7]     VGP3[6]    VGP3[5]    VGP3[4]    VGP3[3]    VGP3[2]     VGP3[1]   VGP3[0]
   MODE         R          R           R          R          R          R          R         R
VGP3[15:0] ········GP3 电压值，无符号二进制，LSB=100μV

VGP4 寄存器
   0x17        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP4[15]   VGP4[14]    VGP4[13]   VGP4[12]   VGP4[11]   VGP4[10]    VGP4[9]   VGP4[8]
   MODE         R          R           R          R          R          R          R         R

   0x18        BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VGP4[7]     VGP4[6]    VGP4[5]    VGP4[4]    VGP4[3]    VGP4[2]     VGP4[1]   VGP4[0]
   MODE         R          R           R          R          R          R          R         R


南京集澈电子科技有限公司                                     1.2                                         9 / 33
宜矽源半导体南京有限公司
```

## PDF 第 10 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

```text
DVC1124-2 参考手册                                                        4 串至 24 串电池组监控芯片
VGP4[15:0] ········GP4 电压值，无符号二进制，LSB=100μV

VGP5 寄存器
   0x19        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME      VGP5[15]   VGP5[14]   VGP5[13]     VGP5[12]   VGP5[11]    VGP5[10]    VGP5[9]   VGP5[8]
   MODE         R          R          R            R          R           R           R        R

   0x1A        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME      VGP5[7]     VGP5[6]    VGP5[5]     VGP5[4]    VGP5[3]      VGP5[2]    VGP5[1]   VGP5[0]
   MODE         R          R          R            R          R           R           R        R
VGP5[15:0] ········GP5 电压值，无符号二进制，LSB=100μV

VGP6 寄存器
   0x1B        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME      VGP6[15]   VGP6[14]   VGP6[13]     VGP6[12]   VGP6[11]    VGP6[10]    VGP6[9]   VGP6[8]
   MODE         R          R          R            R          R           R           R        R

   0x1C        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME      VGP6[7]     VGP6[6]    VGP6[5]     VGP6[4]    VGP6[3]      VGP6[2]    VGP6[1]   VGP6[0]
   MODE         R          R          R            R          R           R           R        R
VGP6[15:0] ········GP6 电压值，无符号二进制，LSB=100μV

VC1 寄存器
   0x1D        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC1[15]    VC1[14]    VC1[13]     VC1[12]    VC1[11]      VC1[10]     VC1[9]   VC1[8]
   MODE         R          R          R            R          R           R           R        R

   0x1E        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC1[7]     VC1[6]     VC1[5]       VC1[4]     VC1[3]      VC1[2]      VC1[1]   VC1[0]
   MODE         R          R          R            R          R           R           R        R
VC1[15:0]···········C1 电压值，默认无符号二进制，LSB=100μV

VC2 寄存器
   0x1F        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC2[15]    VC2[14]    VC2[13]     VC2[12]    VC2[11]      VC2[10]     VC2[9]   VC2[8]
   MODE         R          R          R            R          R           R           R        R
   0x20        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC2[7]     VC2[6]     VC2[5]       VC2[4]     VC2[3]      VC2[2]      VC2[1]   VC2[0]
   MODE         R          R          R            R          R           R           R        R
VC2[15:0]···········C2 电压值，默认无符号二进制，LSB=100μV

VC3 寄存器
   0x21        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC3[15]    VC3[14]    VC3[13]     VC3[12]    VC3[11]      VC3[10]     VC3[9]   VC3[8]
   MODE         R          R          R            R          R           R           R        R

   0x22        BIT 7      BIT 6      BIT 5       BIT 4      BIT 3        BIT 2      BIT 1     BIT 0
   NAME       VC3[7]     VC3[6]     VC3[5]       VC3[4]     VC3[3]      VC3[2]      VC3[1]   VC3[0]
   MODE         R          R          R            R          R           R           R        R


 10 / 33                                          1.2                            南京集澈电子科技有限公司
                                                                                 宜矽源半导体南京有限公司
```

## PDF 第 11 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

```text
4 串至 24 串电池组监控芯片                                                             DVC1124-2 参考手册
VC3[15:0]···········C3 电压值，默认无符号二进制，LSB=100μV

VC4 寄存器
   0x23       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME      VC4[15]    VC4[14]     VC4[13]     VC4[12]   VC4[11]   VC4[10]     VC4[9]   VC4[8]
   MODE         R          R          R           R         R         R           R        R

   0x24       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME       VC4[7]     VC4[6]     VC4[5]      VC4[4]    VC4[3]    VC4[2]      VC4[1]   VC4[0]
   MODE         R          R          R           R         R         R           R        R
VC4[15:0]···········C4 电压值，默认无符号二进制，LSB=100μV

VC5 寄存器
   0x25       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME      VC5[15]    VC5[14]     VC5[13]     VC5[12]   VC5[11]   VC5[10]     VC5[9]   VC5[8]
   MODE         R          R          R           R         R         R           R        R

   0x26       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME       VC5[7]     VC5[6]     VC5[5]      VC5[4]    VC5[3]    VC5[2]      VC5[1]   VC5[0]
   MODE         R          R          R           R         R         R           R        R
VC5[15:0]···········C5 电压值，默认无符号二进制，LSB=100μV

VC6 寄存器
   0x27       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME      VC6[15]    VC6[14]     VC6[13]     VC6[12]   VC6[11]   VC6[10]     VC6[9]   VC6[8]
   MODE         R          R          R           R         R         R           R        R

   0x28       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME       VC6[7]     VC6[6]     VC6[5]      VC6[4]    VC6[3]    VC6[2]      VC6[1]   VC6[0]
   MODE         R          R          R           R         R         R           R        R
VC6[15:0]···········C6 电压值，默认无符号二进制，LSB=100μV

VC7 寄存器
   0x29       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME      VC7[15]    VC7[14]     VC7[13]     VC7[12]   VC7[11]   VC7[10]     VC7[9]   VC7[8]
   MODE         R          R          R           R         R         R           R        R
   0x2A       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME       VC7[7]     VC7[6]     VC7[5]      VC7[4]    VC7[3]    VC7[2]      VC7[1]   VC7[0]
   MODE         R          R          R           R         R         R           R        R
VC7[15:0]···········C7 电压值，默认无符号二进制，LSB=100μV

VC8 寄存器
   0x2B       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME      VC8[15]    VC8[14]     VC8[13]     VC8[12]   VC8[11]   VC8[10]     VC8[9]   VC8[8]
   MODE         R          R          R           R         R         R           R        R

   0x2C       BIT 7      BIT 6       BIT 5       BIT 4     BIT 3     BIT 2      BIT 1    BIT 0
   NAME       VC8[7]     VC8[6]     VC8[5]      VC8[4]    VC8[3]    VC8[2]      VC8[1]   VC8[0]
   MODE         R          R          R           R         R         R           R        R


南京集澈电子科技有限公司                                     1.2                                     11 / 33
宜矽源半导体南京有限公司
```

## PDF 第 12 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

```text
DVC1124-2 参考手册                                                         4 串至 24 串电池组监控芯片
VC8[15:0]···········C8 电压值，默认无符号二进制，LSB=100μV

VC9 寄存器
   0x2D       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC9[15]     VC9[14]    VC9[13]      VC9[12]    VC9[11]      VC9[10]     VC9[9]    VC9[8]
   MODE         R          R          R             R          R           R           R         R

   0x2E       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME       VC9[7]     VC9[6]     VC9[5]        VC9[4]     VC9[3]      VC9[2]      VC9[1]    VC9[0]
   MODE         R          R          R             R          R           R           R         R
VC9[15:0]···········C9 电压值，默认无符号二进制，LSB=100μV

VC10 寄存器
   0x2F       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC10[15]   VC10[14]   VC10[13]      VC10[12]   VC10[11]    VC10[10]     VC10[9]   VC10[8]
   MODE         R          R          R             R          R           R           R         R

   0x30       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC10[7]     VC10[6]    VC10[5]      VC10[4]    VC10[3]      VC10[2]     VC10[1]   VC10[0]
   MODE         R          R          R             R          R           R           R         R
VC10[15:0]·········C10 电压值，默认无符号二进制，LSB=100μV

VC11 寄存器
   0x31       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC11[15]   VC11[14]   VC11[13]      VC11[12]   VC11[11]    VC11[10]     VC11[9]   VC11[8]
   MODE         R          R          R             R          R           R           R         R

   0x32       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC11[7]     VC11[6]    VC11[5]      VC11[4]    VC11[3]      VC11[2]     VC11[1]   VC11[0]
   MODE         R          R          R             R          R           R           R         R
VC11[15:0] ·········C11 电压值，默认无符号二进制，LSB=100μV

VC12 寄存器
   0x33       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC12[15]   VC12[14]   VC12[13]      VC12[12]   VC12[11]    VC12[10]     VC12[9]   VC12[8]
   MODE         R          R          R             R          R           R           R         R
   0x34       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC12[7]     VC12[6]    VC12[5]      VC12[4]    VC12[3]      VC12[2]     VC12[1]   VC12[0]
   MODE         R          R          R             R          R           R           R         R
VC12[15:0]·········C12 电压值，默认无符号二进制，LSB=100μV

VC13 寄存器
   0x35       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC13[15]   VC13[14]   VC13[13]      VC13[12]   VC13[11]    VC13[10]     VC13[9]   VC13[8]
   MODE         R          R          R             R          R           R           R         R

   0x36       BIT 7       BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC13[7]     VC13[6]    VC13[5]      VC13[4]    VC13[3]      VC13[2]     VC13[1]   VC13[0]
   MODE         R          R          R             R          R           R           R         R


 12 / 33                                          1.2                             南京集澈电子科技有限公司
                                                                                  宜矽源半导体南京有限公司
```

## PDF 第 13 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

```text
4 串至 24 串电池组监控芯片                                                               DVC1124-2 参考手册
VC13[15:0]·········C13 电压值，默认无符号二进制，LSB=100μV

VC14 寄存器
   0x37       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC14[15]   VC14[14]   VC14[13]     VC14[12]   VC14[11]   VC14[10]    VC14[9]   VC14[8]
   MODE         R          R          R            R          R          R          R         R

   0x38       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC14[7]    VC14[6]    VC14[5]      VC14[4]    VC14[3]    VC14[2]     VC14[1]   VC14[0]
   MODE         R          R          R            R          R          R          R         R
VC14[15:0]·········C14 电压值，默认无符号二进制，LSB=100μV

VC15 寄存器
   0x39       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC15[15]   VC15[14]   VC15[13]     VC15[12]   VC15[11]   VC15[10]    VC15[9]   VC15[8]
   MODE         R          R          R            R          R          R          R         R

   0x3A       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC15[7]    VC15[6]    VC15[5]      VC15[4]    VC15[3]    VC15[2]     VC15[1]   VC15[0]
   MODE         R          R          R            R          R          R          R         R
VC15[15:0]·········C15 电压值，默认无符号二进制，LSB=100μV

VC16 寄存器
   0x3B       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC16[15]   VC16[14]   VC16[13]     VC16[12]   VC16[11]   VC16[10]    VC16[9]   VC16[8]
   MODE         R          R          R            R          R          R          R         R

   0x3C       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC16[7]    VC16[6]    VC16[5]      VC16[4]    VC16[3]    VC16[2]     VC16[1]   VC16[0]
   MODE         R          R          R            R          R          R          R         R
VC16[15:0]·········C16 电压值，默认无符号二进制，LSB=100μV

VC17 寄存器
   0x3D       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC17[15]   VC17[14]   VC17[13]     VC17[12]   VC17[11]   VC17[10]    VC17[9]   VC17[8]
   MODE         R          R          R            R          R          R          R         R
   0x3E       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC17[7]    VC17[6]    VC17[5]      VC17[4]    VC17[3]    VC17[2]     VC17[1]   VC17[0]
   MODE         R          R          R            R          R          R          R         R
VC17[15:0]·········C17 电压值，默认无符号二进制，LSB=100μV

VC18 寄存器
   0x3F       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC18[15]   VC18[14]   VC18[13]     VC18[12]   VC18[11]   VC18[10]    VC18[9]   VC18[8]
   MODE         R          R          R            R          R          R          R         R

   0x40       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
   NAME      VC18[7]    VC18[6]    VC18[5]      VC18[4]    VC18[3]    VC18[2]     VC18[1]   VC18[0]
   MODE         R          R          R            R          R          R          R         R


南京集澈电子科技有限公司                                     1.2                                        13 / 33
宜矽源半导体南京有限公司
```

## PDF 第 14 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

```text
DVC1124-2 参考手册                                                        4 串至 24 串电池组监控芯片
VC18[15:0]·········C18 电压值，默认无符号二进制，LSB=100μV

VC19 寄存器
   0x41       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC19[15]   VC19[14]   VC19[13]     VC19[12]   VC19[11]    VC19[10]     VC19[9]   VC19[8]
   MODE         R          R          R            R          R           R           R         R

   0x42       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC19[7]    VC19[6]    VC19[5]      VC19[4]    VC19[3]      VC19[2]     VC19[1]   VC19[0]
   MODE         R          R          R            R          R           R           R         R
VC19[15:0]·········C19 电压值，默认无符号二进制，LSB=100μV

VC20 寄存器
   0x43       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC20[15]   VC20[14]   VC20[13]     VC20[12]   VC20[11]    VC20[10]     VC20[9]   VC20[8]
   MODE         R          R          R            R          R           R           R         R

   0x44       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC20[7]    VC20[6]    VC20[5]      VC20[4]    VC20[3]      VC20[2]     VC20[1]   VC20[0]
   MODE         R          R          R            R          R           R           R         R
VC20[15:0]·········C20 电压值，默认无符号二进制，LSB=100μV

VC21 寄存器
   0x45       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC21[15]   VC21[14]   VC21[13]     VC21[12]   VC21[11]    VC21[10]     VC21[9]   VC21[8]
   MODE         R          R          R            R          R           R           R         R

   0x46       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC21[7]    VC21[6]    VC21[5]      VC21[4]    VC21[3]      VC21[2]     VC21[1]   VC21[0]
   MODE         R          R          R            R          R           R           R         R
VC21[15:0]·········C21 电压值，默认无符号二进制，LSB=100μV

VC22 寄存器
   0x47       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC22[15]   VC22[14]   VC22[13]     VC22[12]   VC22[11]    VC22[10]     VC22[9]   VC22[8]
   MODE         R          R          R            R          R           R           R         R
   0x48       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC22[7]    VC22[6]    VC22[5]      VC22[4]    VC22[3]      VC22[2]     VC22[1]   VC22[0]
   MODE         R          R          R            R          R           R           R         R
VC22[15:0]·········C22 电压值，默认无符号二进制，LSB=100μV

VC23 寄存器
   0x49       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC23[15]   VC23[14]   VC23[13]     VC23[12]   VC23[11]    VC23[10]     VC23[9]   VC23[8]
   MODE         R          R          R            R          R           R           R         R

   0x4A       BIT 7      BIT 6      BIT 5        BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
   NAME      VC23[7]    VC23[6]    VC23[5]      VC23[4]    VC23[3]      VC23[2]     VC23[1]   VC23[0]
   MODE         R          R          R            R          R           R           R         R


 14 / 33                                         1.2                             南京集澈电子科技有限公司
                                                                                 宜矽源半导体南京有限公司
```

## PDF 第 15 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

```text
4 串至 24 串电池组监控芯片                                                                            DVC1124-2 参考手册
VC23[15:0]·········C23 电压值，默认无符号二进制，LSB=100μV

VC24 寄存器
     0x4B             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            VC24[15]           VC24[14]   VC24[13]   VC24[12]   VC24[11]   VC24[10]    VC24[9]   VC24[8]
    MODE                R                 R          R          R          R          R          R         R

     0x4C             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            VC24[7]            VC24[6]    VC24[5]    VC24[4]    VC24[3]    VC24[2]     VC24[1]   VC24[0]
    MODE                R                 R          R          R          R          R          R         R
VC24[15:0]·········C24 电压值，默认无符号二进制，LSB=100μV

VVOS 寄存器
     0x4D             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            VVOS[15]           VVOS[14]   VVOS[13]   VVOS[12]   VVOS[11]   VVOS[10]    VVOS[9]   VVOS[8]
    MODE                R                 R          R          R          R          R          R         R

     0x4E             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            VVOS[7]            VVOS[6]    VVOS[5]    VVOS[4]    VVOS[3]    VVOS[2]     VVOS[1]   VVOS[0]
    MODE                R                 R          R          R          R          R          R         R
VVOS[15:0] ········VADC 校准电压值，无符号二进制，LSB=100μV

CVOS 寄存器
     0x4F             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            CVOS[15]           CVOS[14]   CVOS[13]   CVOS[12]   CVOS[11]   CVOS[10]    CVOS[9]   CVOS[8]
    MODE                R                 R          R          R          R          R          R         R

     0x50             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME            CVOS[7]            CVOS[6]    CVOS[5]    CVOS[4]    CVOS[3]    CVOS[2]     CVOS[1]   CVOS[0]
    MODE                R                 R          R          R          R          R          R         R
CVOS[15:0] ········CADC 校准电压值，有符号二进制补码，LSB=5μV

充/放电控制寄存器
     0x51             BIT 7             BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1     BIT 0
    NAME              LDPU              PDSGC      PCHGC      DSGM      DSGC[1]    DSGC[0]     CHGC[1]   CHGC[0]
    MODE              R/W                R/W        R/W        R/W        R/W        R/W        R/W       R/W
   DEFAULT              0                 0          0          0          0          0          0         0
LDPU ··················LOAD 上拉控制位
0·························关闭 LOAD 上拉
1·························开启 LOAD 上拉，该 bit 会在 60s 后自动复位为 0
PDSGC ··············PDSG 驱动输出控制位
0·························关闭 PDSG 驱动输出
1·························开启 PDSG 驱动输出
PCHGC ···············PCHG 驱动输出控制位
0·························关闭 PCHG 驱动输出
1·························开启 PCHG 驱动输出
DSGM ················DSG 驱动输出模式控制位
0·························DSG 为电荷泵驱动输出模式


南京集澈电子科技有限公司                                                   1.2                                        15 / 33
宜矽源半导体南京有限公司
```

## PDF 第 16 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

```text
DVC1124-2 参考手册                                                           4 串至 24 串电池组监控芯片
1·························DSG 为源随驱动输出模式
DSGC[1:0] ··········DSG 驱动输出控制位
00/01 ·················关闭 DSG 驱动输出
10·······················关闭 DSG 驱动输出，但允许在充电电流大于 FET 体二极管续流阈值时开启 DSG 驱动输出
11·······················开启 DSG 驱动输出
CHGC[1:0] ··········CHG 驱动输出控制位
00/01 ·················关闭 CHG 驱动输出
10·······················关闭 CHG 驱动输出，但允许在放电电流大于 FET 体二极管续流阈值时开启 CHG 驱动输出
11·······················开启 CHG 驱动输出
      0x52              BIT 7      BIT 6       BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
     NAME              PDWM                            DPC[4]   DPC[3]     DPC[2]     DPC[1]   DPC[0]
     MODE                   R/W    R/W         R/W      R/W      R/W        R/W        R/W      R/W
    DEFAULT                  1         0        0        1        0          0          0        0
PDWM ···············I2C 超时关闭 PDSG 驱动输出屏蔽位
0·························I2C 超时关闭 PDSG 驱动输出
1·························I2C 超时不影响 PDSG 驱动输出状态
DPC[4:0] ············DSG 驱动输出下拉强度控制位，
00000·················下拉强度为 0
00001·················下拉强度为 1
……
11101·················下拉强度为 29
11110·················下拉强度为 30
11111·················N/A

放电屏蔽控制寄存器
      0x53              BIT 7      BIT 6       BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
     NAME               PDDM      PCWM         PCCM    PCDM     DWM        DDM        DPDM     DBDM
     MODE                   R/W    R/W         R/W      R/W      R/W        R/W        R/W      R/W
    DEFAULT                  0         1        0        1        1          0          0        1
PDDM ················关闭 PDSG 驱动输出屏蔽位
0·························DON 输入为 0 时关闭 PDSG 驱动输出
1·························DON 输入不影响 PDSG 驱动输出状态
PCWM ···············I2C 超时关闭 PCHG 驱动输出屏蔽位
0·························I2C 超时关闭 PCHG 驱动输出
1·························I2C 超时不影响 PCHG 驱动输出状态
PCCM ·················CON(CHG_OFF_N)关闭 PCHG 驱动输出屏蔽位
0·························CON 输入为 0 时关闭 PCHG 驱动输出
1·························CON 输入不影响 PCHG 驱动输出状态
PCDM ················DON(DSG_OFF_N)关闭 PCHG 驱动输出屏蔽位
0·························DON 输入为 0 时关闭 PCHG 驱动输出
1·························DON 输入不影响 PCHG 驱动输出状态
DWM ·················I2C 超时关闭 DSG 驱动输出屏蔽位
0·························I2C 超时关闭 DSG 驱动输出
1·························I2C 超时不影响 DSG 驱动输出


 16 / 33                                               1.2                         南京集澈电子科技有限公司
                                                                                   宜矽源半导体南京有限公司
```

## PDF 第 17 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

```text
4 串至 24 串电池组监控芯片                                                                DVC1124-2 参考手册
DDM ··················DON(DSG_OFF_N)关闭 DSG 驱动输出屏蔽位
0·························DON 输入为 0 时关闭 DSG 驱动输出
1·························DON 输入不影响 DSG 驱动输出状态
DPDM ················PDSG 开启时关闭 DSG 驱动输出屏蔽位
0·························PDSG 开启时关闭 DSG 驱动输出
1·························PDSG 开启时不影响 DSG 驱动输出状态
DBDM ················放电 NFET 体二极管保护屏蔽位
0·························充电电流大于放电 NFET 体二极管续流阈值时自动开启 DSG 驱动输出
1·························充电电流大于放电 NFET 体二极管续流阈值时不影响 DSG 驱动输出状态

充电屏蔽控制寄存器
     0x54          BIT 7          BIT 6         BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME           CWM           CO1M           CO2M    CSM     CDM     CCM        CPCM    CBDM
    MODE           R/W            R/W           R/W     R/W     R/W     R/W        R/W     R/W
   DEFAULT           1              1            1       1       1       0          0       1
CWM ·················I2C 超时关闭 CHG 驱动输出屏蔽位
0·························I2C 超时关闭 CHG 驱动输出
1·························I2C 超时不影响 CHG 驱动输出
CO1M ················1 级放电过流时关闭 CHG 驱动输出屏蔽位
0·························1 级放电过流时关闭 CHG 驱动输出
1·························1 级放电过流时不影响 CHG 驱动输出
CO2M ················2 级放电过流时关闭 CHG 驱动输出屏蔽位
0·························2 级放电过流时关闭 CHG 驱动输出
1·························2 级放电过流时不影响 CHG 驱动输出
CSM ···················放电短路时关闭 CHG 驱动输出屏蔽位
0·························放电短路时关闭 CHG 驱动输出
1·························放电短路时不影响 CHG 驱动输出
CDM···················DON(DSG_OFF_N)关闭 CHG 驱动输出屏蔽位
0·························DON 输入为 0 时关闭 CHG 驱动输出
1·························DON 输入不影响 CHG 驱动输出状态
CCM ···················CON(CHG_OFF_N)关闭 CHG 驱动输出屏蔽位
0·························CON 输入为 0 时关闭 CHG 驱动输出
1·························CON 输入不影响 CHG 驱动输出状态
CPCM ·················PCHG 开启时关闭 CHG 驱动输出屏蔽位
0·························PCHG 开启时关闭 CHG 驱动输出
1·························PCHG 开启时不影响 CHG 驱动输出状态
CBDM ················充电 NFET 体二极管保护屏蔽位
0·························放电电流大于充电 NFET 体二极管续流阈值时自动开启 CHG 驱动输出
1·························放电电流大于充电 NFET 体二极管续流阈值时不影响 CHG 驱动输出状态

CADC 控制寄存器
     0x55          BIT 7          BIT 6         BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME           HSFM                                         CAEW    CAES               CAMZ
    MODE           R/W            R/W           R/W     R/W     R/W     R/W        R/W     R/W1
   DEFAULT           0              0            1       0       1       1          0       0

南京集澈电子科技有限公司                                            1.2                                17 / 33
宜矽源半导体南京有限公司
```

## PDF 第 18 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

```text
DVC1124-2 参考手册                                                                 4 串至 24 串电池组监控芯片
HSFM ·················高边 FET 驱动输出屏蔽位
0·························允许高边 FET 驱动输出
1·························屏蔽高边 FET 驱动输出
CAEW·················芯片工作状态下 CADC 使能控制位
0·························芯片工作状态下关闭 CADC
1·························芯片工作状态下开启 CADC
CAES ··················芯片休眠状态下电流唤醒使能控制位
0·························芯片休眠状态下关闭电流唤醒
1·························芯片休眠状态下开启电流唤醒
CAMZ ·················CADC 手动校准控制位
0·························无效
1·························开启一次 CADC 校准，该 bit 会自动复位为 0
       0x56                BIT 7     BIT 6      BIT 5     BIT 4      BIT 3       BIT 2      BIT 1      BIT 0
      NAME                                                          C1OW[1]     C1OW[0]    C1OS[1]    C1OS[0]
     MODE                  R/W        R/W       R/W        R/W        R/W         R/W        R/W        R/W
    DEFAULT                     1      1         1          1          1           1          1          1
       0x57                BIT 7     BIT 6      BIT 5     BIT 4      BIT 3       BIT 2      BIT 1      BIT 0
      NAME
     MODE                       R      R         R          R          R           R          R          R
    DEFAULT                     0      0         1          0          1           0          0          0

       0x58                BIT 7     BIT 6      BIT 5     BIT 4      BIT 3       BIT 2      BIT 1      BIT 0
      NAME
     MODE                       R      R         R          R          R           R          R          R
    DEFAULT                     0      0         0          0          0           1          0          1
C1OW[1:0] ·········CADC CC1 测量时间控制位
00·······················0.5ms
01·······················1.0ms
10·······················2.0ms
11·······················4.0ms
C1OS[1:0] ··········芯片休眠状态下电流唤醒时间控制位
00·······················4ms
01·······················8ms
10·······················16ms
11·······················32ms

1 级放电过流保护阈值寄存器
       0x59                BIT 7     BIT 6      BIT 5     BIT 4      BIT 3       BIT 2      BIT 1      BIT 0
      NAME              OCD1T[7]    OCD1T[6]   OCD1T[5   OCD1T[4]   OCD1T[3]    OCD1T[2]   OCD1T[1]   OCD1T[0]
     MODE                  R/W        R/W       R/W        R/W        R/W         R/W        R/W        R/W
    DEFAULT                     0      0         0          0          0           0          0          0
OCD1T[7:0] ········1 级放电过流保护阈值控制位
0x00 ···················关闭
其他 ···················阈值电压=OCD1T×0.25mV



  18 / 33                                                  1.2                           南京集澈电子科技有限公司
                                                                                         宜矽源半导体南京有限公司
```

## PDF 第 19 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

```text
4 串至 24 串电池组监控芯片                                                                         DVC1124-2 参考手册

1 级充电过流保护阈值寄存器
      0x5A                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME              OCC1T[7]    OCC1T[6]   OCC1T[5]   OCC1T[4]   OCC1T[3]   OCC1T[2]    OCC1T[1]   OCC1T[0]
     MODE                  R/W       R/W        R/W        R/W        R/W        R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0          0           0          0
OCC1T[7:0] ········1 级充电过流保护阈值控制位
0x00 ···················关闭
其他 ···················阈值电压=OCC1T×0.25mV

1 级放电过流保护延迟寄存器
      0x5B                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME              OCD1D[7]    OCD1D[6]   OCD1D[5]   OCD1D[4]   OCD1D[3]   OCD1D[2]    OCD1D[1]   OCD1D[0]
     MODE                  R/W       R/W        R/W        R/W        R/W        R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0          0           0          0
OCD1D[7:0] ·······1 级放电过流保护延迟控制位，延迟时间=(OCD1D+1)×8ms

1 级充电过流保护延迟寄存器
      0x5C                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME              OCC1D[7]    OCC1D[6]   OCC1D[5]   OCC1D[4]   OCC1D[3]   OCC1D[2]    OCC1D[1]   OCC1D[0]
     MODE                  R/W       R/W        R/W        R/W        R/W        R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0          0           0          0
      0x5D                BIT 7      BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME
     MODE                  R/W       R/W        R/W        R/W         R          R           R          R
    DEFAULT                    1      0          1          1           -          -           -          -
OCCD[7:0]··········1 级充电过流保护延迟控制位，延迟时间=(OCC1D+1)×8ms

2 级放电过流保护阈值寄存器
      0x5E                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME                           OCD2E     OCD2T[5]   OCD2T[4]   OCD2T[3]   OCD2T[2]    OCD2T[1]   OCD2T[0]
     MODE                      R     R/W        R/W        R/W        R/W        R/W         R/W        R/W
    DEFAULT                    0      1          0          0          0          0           0          0
OCD2E ···············2 级放电过流保护使能控制位
0·························关闭
1·························开启
OCD2T[5:0] ········2 级放电过流保护阈值控制位，阈值电压=(OCD2T+1)×4mV

2 级充电过流保护阈值寄存器
      0x5F                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3      BIT 2       BIT 1      BIT 0
     NAME                           OCC2E     OCC2T[5]   OCC2T[4]   OCC2T[3]   OCC2T[2]    OCC2T[1]   OCC2T[0]
     MODE                  R/W       R/W        R/W        R/W        R/W        R/W         R/W        R/W
    DEFAULT                    1      1          0          0          0          0           0          0
OCC2E ················2 级充电过流保护使能控制位
0·························关闭
1·························开启


南京集澈电子科技有限公司                                               1.2                                         19 / 33
宜矽源半导体南京有限公司
```

## PDF 第 20 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

```text
DVC1124-2 参考手册                                                                 4 串至 24 串电池组监控芯片
OCC2T[5:0] ········2 级充电过流保护阈值控制位，阈值电压=(OCC2T+1)×4mV

2 级放电过流保护延迟寄存器
      0x60                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME              OCD2D[7]    OCD2D[6]   OCD2D[5]   OCD2D[4]   OCD2D[3]    OCD2D[2]    OCD2D[1]   OCD2D[0]
     MODE                  R/W       R/W        R/W        R/W        R/W         R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0           0           0          0
OCD2D[7:0]·······2 级放电过流保护延迟控制位，延迟时间=(OCD2D+1)×4ms

2 级充电过流保护延迟寄存器
      0x61                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME              OCC2D[7]    OCC2D[6]   OCC2D[5]   OCC2D[4]   OCC2D[3]    OCC2D[2]    OCC2D[1]   OCC2D[0]
     MODE                  R/W       R/W        R/W        R/W        R/W         R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0           0           0          0
OCC2D[7:0]········2 级充电过流保护延迟控制位，延迟时间=(OCC2D+1)×4ms

放电短路保护阈值寄存器
      0x62                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME                           SCDE      SCDT[5]    SCDT[4]    SCDT[3]      SCDT[2]     SCDT[1]   SCDT[0]
     MODE                      R     R/W        R/W        R/W        R/W         R/W         R/W        R/W
    DEFAULT                    0      1          0          0          0           0           0          0
SCDE ··················放电短路保护使能控制位
0·························关闭
1·························开启
SCDT[5:0]···········放电短路保护阈值控制位，阈值电压=SCDT×10mV

放电短路保护延迟寄存器
      0x63                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME               SCDD[7]    SCDD[6]    SCDD[5]    SCDD[4]    SCDD[3]      SCDD[2]    SCDD[1]    SCDD[0]
     MODE                  R/W       R/W        R/W        R/W        R/W         R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0           0           0          0

      0x64                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME
     MODE                      R      R          R          R          R           R           R          R
 SCDD[7:0] ········放电短路保护延迟控制位，延迟时间=SCDD×7.81μs

电流唤醒寄存器
      0x65                BIT 7     BIT 6      BIT 5      BIT 4      BIT 3        BIT 2       BIT 1     BIT 0
     NAME                CWT[7]     CWT[6]     CWT[5]     CWT[4]     CWT[3]      CWT[2]      CWT[1]     CWT[0]
     MODE                  R/W       R/W        R/W        R/W        R/W         R/W         R/W        R/W
    DEFAULT                    0      0          0          0          0           0           0          0
CWT[7:0] ···········芯片休眠状态下电流唤醒阈值控制位
0x00 ···················关闭休眠状态下电流唤醒
其他 ···················阈值电压=CWT×10μV




 20 / 33                                                   1.2                            南京集澈电子科技有限公司
                                                                                          宜矽源半导体南京有限公司
```

## PDF 第 21 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)

```text
4 串至 24 串电池组监控芯片                                                                    DVC1124-2 参考手册

续流保护寄存器
     0x66           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME           BDPT[7]        BDPT[6]    BDPT[5]   BDPT[4]   BDPT[3]   BDPT[2]     BDPT[1]   BDPT[0]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0
BDPT[7:0] ··········充/放电 NFET 体二极管续流保护阈值控制位
0x00 ···················关闭充/放电 NFET 体二极管续流保护
其他 ···················阈值电压=BDPT×40μV

电池被动均衡寄存器
     0x67           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME            CB[24]         CB[23]    CB[22]    CB[21]    CB[20]    CB[19]      CB[18]    CB[17]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0

     0x68           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME            CB[16]         CB[15]    CB[14]    CB[13]    CB[12]    CB[11]      CB[10]     CB[9]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0

     0x69           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME            CB[8]           CB[7]     CB[6]     CB[5]     CB[4]     CB[3]       CB[2]     CB[1]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0
CB[n]··················第 n 节电池被动均衡控制位
0·························关闭第 n 节电池被动均衡
1·························开启第 n 节电池被动均衡，该组寄存器会在 60s 后自动复位为 0

电池屏蔽寄存器
     0x6A           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME           CM[24]          CM[23]    CM[22]    CM[21]    CM[20]    CM[19]      CM[18]    CM[17]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0
     0x6B           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME           CM[16]          CM[15]    CM[14]    CM[13]    CM[12]    CM[11]      CM[10]    CM[9]
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0
     0x6C           BIT 7           BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
    NAME            CM[8]          CM[7]     CM[6]     CM[5]      PKM       LDM         CTM      V1P8M
    MODE             R/W            R/W       R/W       R/W       R/W       R/W         R/W       R/W
   DEFAULT            0                  0     0         0         0         0           0         0
CM[n] ················第 n 节电池保护屏蔽控制位
0·························开启第 n 节电池保护
1·························关闭第 n 节电池保护，同时默认关闭第 n 节电池电压测量
PKM ···················PACK 电压测量屏蔽控制位
0·························开启 PACK 电压测量

南京集澈电子科技有限公司                                            1.2                                      21 / 33
宜矽源半导体南京有限公司
```

## PDF 第 22 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

```text
DVC1124-2 参考手册                                                              4 串至 24 串电池组监控芯片
1·························关闭 PACK 电压测量
LDM ···················LOAD 电压测量屏蔽控制位
0·························开启 LOAD 电压测量
1·························关闭 LOAD 电压测量
CTM ···················芯片核心温度测量屏蔽控制位
0·························开启芯片核心温度测量
1·························关闭芯片核心温度测量
V1P8M ···············V1P8 电压测量屏蔽控制位
0·························开启 V1P8 电压测量
1·························关闭 V1P8 电压测量

电荷泵控制寄存器
      0x6D                   BIT 7   BIT 6    BIT 5      BIT 4     BIT 3      BIT 2      BIT 1    BIT 0
      NAME                                   CPVS[2]    CPVS[1]   CPVS[0]     COW        CMM       CVS
      MODE                    R/W    R/W      R/W        R/W       R/W        R/W         R/W      R/W
    DEFAULT                    0         0      1          0        1          0           0        0
CPVS[2:0]···········电荷泵输出电压控制位
000·····················关闭
001·····················6V
010·····················7V
011·····················8V
100·····················9V
101·····················10V
110·····················11V
111·····················12V
COW ··················电池采集线断线检测控制位
0·························关闭
1·························开启断线检测，该 bit 会在 1s 后自动复位为 0
CMM··················屏蔽电池电压测量控制位
0·························关闭屏蔽电池电压测量
1·························开启屏蔽电池电压测量
CVS ····················电池电压有符号数显示控制位
0·························电池电压以无符号数显示，LSB=100μV
1·························电池电压以有符号数显示，LSB=200μV

VADC 控制寄存器
       0x6E                  BIT 7   BIT 6    BIT 5      BIT 4     BIT 3      BIT 2      BIT 1    BIT 0
      NAME                    VAE    VASM    VAMP[1]    VAMP[0]                          VAO[1]   VAO[0]
     MODE                     R/W    R/W      R/W        R/W       R/W        R/W         R/W      R/W
    DEFAULT                    1         1     0          0         1          1           0        1
       0x6F                  BIT 7   BIT 6    BIT 5      BIT 4     BIT 3      BIT 2      BIT 1    BIT 0
      NAME
     MODE                      R         R     R          R         R          R           R        R
    DEFAULT                    0         0     1          0         1          0           0        0
VAE ····················VADC 使能控制位

  22 / 33                                                1.2                          南京集澈电子科技有限公司
                                                                                      宜矽源半导体南京有限公司
```

## PDF 第 23 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

```text
4 串至 24 串电池组监控芯片                                                                         DVC1124-2 参考手册
0·························关闭
1·························开启
VASM ·················VADC 与 CADC CC2 同步测量控制位
0·························VADC 连续测量
1·························VADC 与 CADC CC2 同步测量
VAMP[1:0] ·········VADC 同步测量周期控制位
00·······················每 1 个 CC2 周期 VADC 测量 1 次
01·······················每 2 个 CC2 周期 VADC 测量 1 次
10·······················每 4 个 CC2 周期 VADC 测量 1 次
11·······················每 8 个 CC2 周期 VADC 测量 1 次
VAO[1:0] ············VADC 测量时间控制位
00·······················0.79ms
01·······················1.54ms
10·······················3.03ms
11·······················6.02ms

电池过压保护寄存器
      0x70                   BIT 7     BIT 6        BIT 5    BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME               COVT[11]      COVT[10]   COVT[9]    COVT[8]   COVT[7]   COVT[6]     COVT[5]   COVT[4]
     MODE                    R/W        R/W         R/W      R/W       R/W       R/W         R/W       R/W
    DEFAULT                    0         0           0        0         0         0           0         0

      0x71                   BIT 7     BIT 6        BIT 5    BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME                   COVT[3]   COVT[2]    COVT[1]    COVT[0]   COVD[3]   COVD[2]     COVD[1]   COVD[0]
     MODE                    R/W        R/W         R/W      R/W       R/W       R/W         R/W       R/W
    DEFAULT                    0         0           0        0         0         0           0         0
COVT[11:0] ········电池过压保护阈值控制位
0x00 ···················关闭电池过压保护
其他 ···················阈值电压=COVT×1mV+500mV
COVD[3:0] ·········电池过压保护延迟控制位
0000···················200ms
0001···················300ms
0010···················400ms
0011···················500ms
0100···················600ms
0101···················700ms
0110···················800ms
0111···················900ms
1000···················1s
1001···················2s
1010···················3s
1011···················4s
1100···················5s
1101···················6s
1110···················7s


南京集澈电子科技有限公司                                                 1.2                                       23 / 33
宜矽源半导体南京有限公司
```

## PDF 第 24 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)

```text
DVC1124-2 参考手册                                                                 4 串至 24 串电池组监控芯片
1111···················8s

电压欠压保护寄存器
      0x72                   BIT 7     BIT 6      BIT 5     BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME               CUVT[11]      CUVT[10]   CUVT[9]   CUVT[8]   CUVT[7]     CUVT[6]    CUVT[5]   CUVT[4]
     MODE                    R/W        R/W       R/W       R/W       R/W         R/W        R/W       R/W
    DEFAULT                   0          0          0         0         0           0          0         0

      0x73                   BIT 7     BIT 6      BIT 5     BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME                   CUVT[3]   CUVT[2]    CUVT[1]   CUVT[0]   CUVD[3]     CUVD[2]    CUVD[1]   CUVD[0]
     MODE                    R/W        R/W       R/W       R/W       R/W         R/W        R/W       R/W
    DEFAULT                   0          0          0         0         0           0          0         0
CUVT[11:0] ········电池欠压保护阈值控制位
0x00 ···················关闭电池欠压保护
其他 ···················阈值电压=CUVT×1mV
CUVD[3:0] ·········电池欠压保护延迟控制位
0000···················200ms
0001···················300ms
0010···················400ms
0011···················500ms
0100···················600ms
0101···················700ms
0110···················800ms
0111···················900ms
1000···················1s
1001···················2s
1010···················3s
1011···················4s
1100···················5s
1101···················6s
1110···················7s
1111···················8s

GP1，GP2 和 GP3 模式寄存器
      0x74                   BIT 7     BIT 6      BIT 5     BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME               GP1M[1]       GP1M[0]    GP2M[2]   GP2M[1]   GP2M[0]     GP3M[2]    GP3M[1]   GP3M[0]
     MODE                    R/W        R/W       R/W       R/W       R/W         R/W        R/W       R/W
    DEFAULT                   0          0          0         0         0           0          0         0
GP1M[1:0] ·········GP1 模式控制位
00·······················关闭，GP1 为高阻态
01·······················热敏电阻检测输入
10·······················模拟电压检测输入
11·······················CON(CHG_OFF_N)，CHG 驱动硬线控制，低电平关闭 CHG 驱动输出，高电平不影响 CHG 驱动输出状态
GP2M[2:0] ·········GP2 模式控制位
000·····················关闭，GP2 为高阻态
001·····················热敏电阻检测输入

 24 / 33                                                    1.2                           南京集澈电子科技有限公司
                                                                                          宜矽源半导体南京有限公司
```

## PDF 第 25 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

```text
4 串至 24 串电池组监控芯片                                                                     DVC1124-2 参考手册
010·····················模拟电压检测输入
011·····················N/A
100·····················N/A
101·····················N/A
110·····················中断输出
111·····················低边 PDSG 驱动输出
GP3M[2:0] ·········GP3 模式控制位
000·····················关闭，GP3 为高阻态
001·····················热敏电阻检测输入
010·····················模拟电压检测输入
011·····················N/A
100·····················N/A
101·····················N/A
110·····················中断输出
111·····················低边 PCHG 驱动输出

GP4，GP5 和 GP6 模式寄存器
      0x75                BIT 7      BIT 6     BIT 5     BIT 4     BIT 3     BIT 2       BIT 1     BIT 0
     NAME               GP4M[1]     GP4M[0]   GP5M[2]   GP5M[1]   GP5M[0]   GP6M[2]     GP6M[1]   GP6M[0]
     MODE                     R/W    R/W       R/W       R/W       R/W       R/W         R/W       R/W
    DEFAULT                    0       0         0         0         0         0           0         0
GP4M[1:0] ·········GP4 模式控制位
00·······················关闭，GP4 为高阻态
01·······················热敏电阻检测输入
10·······················模拟电压检测输入
11·······················DON(DSG_OFF_N)，DSG 驱动硬线控制，低电平关闭 DSG 驱动输出，高电平不影响 DSG 驱动输出状态
GP5M[2:0] ·········GP5 模式控制位
000·····················关闭，GP5 为高阻态
001·····················热敏电阻检测输入
010·····················模拟电压检测输入
011·····················N/A
100·····················N/A
101·····················N/A
110·····················中断输出
111·····················低边 CHG 驱动输出
GP6M[2:0] ·········GP6 模式控制位
000·····················关闭，GP6 为高阻态
001·····················热敏电阻检测输入
010·····················模拟电压检测输入
011·····················N/A
100·····················N/A
101·····················N/A
110·····················中断输出
111·····················低边 DSG 驱动输出



南京集澈电子科技有限公司                                             1.2                                       25 / 33
宜矽源半导体南京有限公司
```

## PDF 第 26 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

```text
DVC1124-2 参考手册                                                                    4 串至 24 串电池组监控芯片

芯片过温保护寄存器
      0x76                BIT 7      BIT 6     BIT 5           BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME                 COTF      COTT[6]   COTT[5]         COTT[4]   COTT[3]     COTT[2]    COTT[1]   COTT[0]
     MODE                      RC    R/W       R/W             R/W       R/W         R/W         R/W      R/W
    DEFAULT                    0      0          0              0         0           0           0        0
COTF ··················芯片内核过温关机标识位，该 bit 会在读取后自动复位为 0
0·························未发生过芯片内核过温
1·························已发生过芯片内核过温
COTT[6:0] ··········芯片内核过温保护阈值控制位,
0x00 ···················关闭芯片内核过温保护
其他 ···················阈值温度=(1466+COTT×2)×0.24467°C-271.03°C

I2C 看门狗寄存器
      0x77                BIT 7      BIT 6     BIT 5           BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME                V3P3ES     V3P3EW    V3P3M                      IWTS       IWT[2]      IWT[1]   IWT[0]
     MODE                  R/W       R/W       R/W              R         R          R/W         R/W      R/W
    DEFAULT                    1      1          0              0         0           0           0        0
V3P3ES ··············芯片休眠状态下 V3P3 输出控制位
0·························芯片休眠状态下关闭 V3P3 输出
1·························芯片休眠状态下开启 V3P3 输出
V3P3EW·············芯片工作状态下 V3P3 输出控制位
0·························芯片工作状态下关闭 V3P3 输出
1·························芯片工作状态下开启 V3P3 输出
V3P3M ···············I2C 超时重启 V3P3 控制位
0·························I2C 超时不影响 V3P3 输出
1·························I2C 超时后 V3P3 输出关闭 1s 后重启
IWTS ··················I2C 看门狗超时状态位
0·························未超时
1·························已超时
IWT[2:0] ············I2C 看门狗定时器控制位
0XX·····················关闭定时器
100·····················定时器设为 4s
101·····················定时器设为 8s
110·····················定时器设为 16s
111·····················定时器设为 32s

定时唤醒寄存器
      0x78                BIT 7      BIT 6     BIT 5           BIT 4     BIT 3       BIT 2      BIT 1     BIT 0
     NAME                 TIWK                                          TWSE[3]     TWSE[2]    TWSE[1]   TWSE[0]
     MODE                      R      R          R              R        R/W         R/W         R/W      R/W
    DEFAULT                    0      0          0              0         0           0           0        0
TIWK ··················定时唤醒状态位
0·························关闭
1·························开启
TWSE[3:0] ··········定时唤醒定时器控制位

 26 / 33                                                       1.2                           南京集澈电子科技有限公司
                                                                                             宜矽源半导体南京有限公司
```

## PDF 第 27 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)

```text
4 串至 24 串电池组监控芯片                                                             DVC1124-2 参考手册
0000···················关闭
0001···················10s
0010···················20s
0011···················30s
0100···················40s
0101···················50s
0110···················1min
0111···················2min
1000···················3min
1001···················4min
1010···················5min
1011···················6min
1100···················7min
1101···················8min
1110···················9min
1111···················10min

中断屏蔽寄存器
      0x79                   BIT 7   BIT 6   BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
     NAME                    IWM     IVOM    ICCM    ICOM    ICUM    IOC1M      IOC2M   ISCDM
     MODE                    R/W     R/W     R/W     R/W     R/W     R/W        R/W     R/W
    DEFAULT                   0       0       0       0       0        0          0       0

      0x7A                   BIT 7   BIT 6   BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
     NAME
     MODE                     R       R       R       R       R        R          R      R
    DEFAULT                   0        -       -       -       -       -          -       -

      0x7B                   BIT 7   BIT 6   BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
     NAME
     MODE                     R       R       R       R       R        R          R      R
    DEFAULT                   0       1       1        -       -       -          -       -

      0x7C                   BIT 7   BIT 6   BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
     NAME
     MODE                     R       R       R       R       R        R          R      R
    DEFAULT                   0       1       0       0       1        0          0       0

      0x7D                   BIT 7   BIT 6   BIT 5   BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
     NAME
     MODE                     R       R       R       R       R        R          R      R
IWM···················唤醒中断输出屏蔽控制位
0·························芯片唤醒后中断输出低电平 1ms
1·························芯片唤醒后中断无输出
IVOM ·················VADC 转换结束中断输出屏蔽控制位
0·························VADC 转换结束后中断输出低电平 1ms
1·························VADC 转换结束后中断无输出
ICCM ··················CADC CC2 转换结束中断输出屏蔽控制位

南京集澈电子科技有限公司                                         1.2                                27 / 33
宜矽源半导体南京有限公司
```

## PDF 第 28 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

```text
DVC1124-2 参考手册                                                            4 串至 24 串电池组监控芯片
0·························CADC CC2 转换结束后中断输出低电平 1ms
1·························CADC CC2 转换结束后中断无输出
ICOM ·················电池过压中断输出屏蔽控制位
0·························电池过压后中断输出低电平 1ms
1·························电池过压后中断无输出
ICUM ·················电池欠压中断输出屏蔽控制位
0·························电池欠压后中断输出低电平 1ms
1·························电池欠压后中断无输出
IOC1M ···············1 级过流(包含 1 级放电过流和 1 级充电过流)中断输出屏蔽控制位
0·························1 级过流后中断输出低电平 1ms
1·························1 级过流后中断无输出
IOC2M ···············2 级过流(包含 2 级放电过流和 2 级充电过流)中断输出屏蔽控制位
0·························2 级过流后中断输出低电平 1ms
1·························2 级过流后中断无输出
ISCDM ················放电短路中断输出屏蔽控制位
0·························放电短路后中断输出低电平 1ms
1·························放电短路后中断无输出

电阻修调寄存器
    0x7E          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME          FRT[7]        FRT[6]        FRT5]     FRT[4]   FRT[3]     FRT[2]     FRT[1]   FRT[0]
    MODE            R             R              R        R        R          R          R        R

    0x7F          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R

    0x80          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R
    0x81          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R
    0x82          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R
    0x83          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R

    0x84          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R

    0x85          BIT 7          BIT 6          BIT 5   BIT 4    BIT 3      BIT 2      BIT 1    BIT 0
    NAME
    MODE            R             R              R        R        R          R          R        R


 28 / 33                                                1.2                         南京集澈电子科技有限公司
                                                                                    宜矽源半导体南京有限公司
```

## PDF 第 29 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

```text
4 串至 24 串电池组监控芯片                                                          DVC1124-2 参考手册
     0x86           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x87           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x88           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x89           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x8A           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x8B           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R
     0x8C           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x8D           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R

     0x8E           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE            R/W      R/W        R           R      R       R          R       R
   DEFAULT            1       0          -          -       -      0          0       0
FRT[7:0] ·············存储在 FUSE 中电阻修调值；电阻值=6800Ω+FRT×25Ω



芯片版本寄存器
     0x8F           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME            CV[7]    CV[6]     CV[5]      CV[4]   CV[3]   CV[2]      CV[1]   CV[0]
    MODE              R       R         R           R      R       R          R       R

     0x90           BIT 7    BIT 6     BIT 5      BIT 4   BIT 3   BIT 2      BIT 1   BIT 0
    NAME
    MODE              R       R         R           R      R       R          R       R
CV[7:0]···············芯片版本




南京集澈电子科技有限公司                                       1.2                               29 / 33
宜矽源半导体南京有限公司
```

## PDF 第 30 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30)

```text
DVC1124-2 参考手册                                                                               4 串至 24 串电池组监控芯片

管理系统

放电管理
放电驱动逻辑
                                                   IWTS

                                                   DWM
                         GP4


          GP4M                                     DDM
                                                                                                            000
                        !=                                                                                  ...
           0x3                                      SCD                                                                  GP6
                                                                                                     INT    110
                                                                                                            111
                                                 DSGC[1]

                                                 DSGC[0]                                            GP6M


                                                                              AND
                                                    CUV                                                                  DSG
                                                                                      HSFM


                                                   OCD1



                                                   OCD2


                                    PDSGF

                                    DPDM

                                    DBDM


                                CC1
                                            >=
                               BDPT


                                                                   图 1 放电驱动逻辑

预放电驱动逻辑
                                                                                                      000
                                                                                                      ...
                                                                                                                  GP2
                                                                                              INT     110
                                                                                                      111
                                             IWTS

                                                           PDWM                              GP2M

                                                           PDSGC            AND
                                 GP4                                                                              PDSG
                                                                                    HSFM

                 GP4M                                      PDDM
                               !=
                  0x3


                                                                  图 2 预放电驱动逻辑




30 / 33                                                               1.2                                   南京集澈电子科技有限公司
                                                                                                            宜矽源半导体南京有限公司
```

## PDF 第 31 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)

```text
4 串至 24 串电池组监控芯片                                                                                                            DVC1124-2 参考手册

充电管理
充电驱动逻辑

                                                 OCD1

                                                 CO1M

                                                 OCD2

                                                 CO2M

                                                     SCD

                                                  CSM

         GP4M                                     IWTS
                       !=                                                                                                   000
          0x3                                    CWM
                                                                                                                            ...
                                                                                                                                         GP5
                                                                                                                      INT   110
                                                                                                                            111
                        GP4
                                                  CDM
                        GP1                                                                                          GP5M


         GP1M                                     CCM                                   AND
                       !=                                                                                                                CHG
          0x3                                 CHGC[1]                                                HSFM

                                              CHGC[0]



                                                     COV



                                                 OCC1



                                                 OCC2


                            PCHGF

                            CPCM

                            CBDM


                               CC1
                                           >=
                              BDPT



                                                                   图 3 充电驱动逻辑

预充电驱动逻辑

                                                                                                                     000
                                                                                                                     ...
                                                IWTS                                                                              GP3
                                                                                                              INT    110
                                                                                                                     111
                GP4M                                       PCWM
                                !=
                 0x3                                       PCHGC                                            GP3M

                                                                                        AND
                                     GP4                                                                                          PCHG
                                                           PCDM                               HSFM
                                     GP1


                GP1M                                       PCCM
                                !=
                 0x3



                                                                   图 4 预充电驱动逻辑

逻辑符号说明
                                      A                             A                         A
                                                !=           Y              >=            Y              <=           Y
                                      B                             B                         B


                                           if (A!=B) Y=1                if (A>=B) Y=1                if (A<=B) Y=1
                                           else Y=0                     else Y=0                     else Y=0


                                                                   图 5 逻辑符号说明




南京集澈电子科技有限公司                                                              1.2                                                                  31 / 33
宜矽源半导体南京有限公司
```

## PDF 第 32 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)

```text
DVC1124-2 参考手册                                                4 串至 24 串电池组监控芯片

应用和实施

电池电压二次校准
      DVC1124 为单颗 24 串电池组监控芯片，电池输入共模电压最高可达 120V，对电池电压测量结果有较
大影响，需要在 MCU 中对电池电压测量值进行二次校准，具体计算公式如下：
          VCBC [n]
VCAC [n]=
           K[n]
K[n]=1+p1 ×VCM [n]+p2 ×VCM [n]2
           0                        n=1
VCM [n]= {
          VCM [n-1]+VCAC [n-1]      n≥2
p1 =0.35×10-6
p2 =-0.12×10-6
其中，
1. VCBC[n]为校准前第 n 串电池电压测量值；
2. VCAC[n]为校准后第 n 串电池电压值；
3. K[n]为第 n 串电池电压校准系数；
4. VCM[n]为第 n 串电池的共模电压。
为简化计算，也可以使用如表 1 所示的校准系数速查表。
                                          表 1 电池电压二次校准系数速查表
                           VCM[V]                                  K
                           0~24                                  1.0000
                           24~40                                 0.9999
                           40~51                                 0.9998
                           51~60                                 0.9997
                           60~68                                 0.9996
                           68~75                                 0.9995
                           75~82                                 0.9994
                           82~88                                 0.9993
                           88~93                                 0.9992
                           93~99                                 0.9991
                          99~104                                 0.9990
                          104~109                                0.9989
                          109~113                                0.9988
                          113~117                                0.9987
                          117~120                                0.9986




 32 / 33                                         1.2              南京集澈电子科技有限公司
                                                                  宜矽源半导体南京有限公司
```

## PDF 第 33 页

来源 [DVC1124-2参考手册_V1.2.pdf PDF p.33](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=33)

```text
4 串至 24 串电池组监控芯片                                           DVC1124-2 参考手册

修订历史记录
 版本       日期        描述                                                 页码
 1.0    2023/4/2    修改“寄存器 CST[3:0]”描述                                  6
                    修改“寄存器 OCD1T[7:0]”描述                                18
                    修改“寄存器 OCC1T[7:0]”描述                                19
                    修改“寄存器 COVT[11:0]”描述                                23
                    修改“寄存器 CUVT[11:0]”描述                                24
                    新增“寄存器 V3P3ES”                                      26
                    新增“寄存器偏移 0x90”                                      29
                    修改“放电驱动逻辑”                                          30
                    修改“预放电驱动逻辑”                                         30
                    修改“充电驱动逻辑”                                          31
                    修改“预充电驱动逻辑”                                         31
                    修改”电池电压二次校准”系数 p2                                   32
 1.1    2025/5/7    寄存器 CPVS[2:0]上电复位值从 101 变更为 000，芯片上电时默认电荷泵为关闭状态     22
                    寄存器 VAE 上电复位值从 1 变更为 0，芯片上电时默认 VADC 为关闭状态           22
 1.2   2025/09/26   寄存器 CPVS[2:0]上电复位值从 000 恢复成 101                     22
                    寄存器 VAE 上电复位值从 0 恢复为 1                              22




南京集澈电子科技有限公司                               1.2                        33 / 33
宜矽源半导体南京有限公司
```
