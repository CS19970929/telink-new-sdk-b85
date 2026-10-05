# BQ76940.pdf 逐页原文提取

用途：检索支持层，非规范化说明。PDF 为最终证据。表格列、上下标、公式、图中连线及希腊字母可能在文本提取中失真。PDF 页从 1 起计；打印页码请直接核对原页。

## PDF 第 1 页

来源 [BQ76940.pdf PDF p.1](../../sources/BQ76940.pdf#page=1)

```text
                                      Product                      Sample &                  Technical                              Tools &                 Support &
                                      Folder                       Buy                       Documents                              Software                Community



                                                                                                                                                             bq76920, bq76930, bq76940
                                                                                                                                     SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015


                            bq769x0 3-Series to 15-Series Cell Battery Monitor Family
                                     for Li-Ion and Phosphate Applications
1 Introduction                                                                                           1




1.1        Features
• AFE Monitoring Features                                                                                  – Undervoltage (UV)
  – Pure digital interface                                                                                 – Secondary protector fault detection
  – Internal ADC measures cell voltage, die                                                              • Additional Features
    temperature, and external thermistor                                                                   – Integrated cell balancing FETs
  – A separate, internal ADC measures pack                                                                 – Charge, discharge low-side NCH FET drivers
    current (coulomb counter)                                                                              – Alert interrupt to host microcontroller
  – Directly supports up to three thermistors                                                              – 2.5-V or 3.3-V output voltage regulator
    (103AT)                                                                                                – No EEPROM programming necessary
• Hardware Protection Features                                                                             – High supply voltage abs max (up to 108 V)
  – Overcurrent in discharge (OCD)                                                                         – Simple I2C™ compatible interface (CRC option)
  – Short circuit in discharge (SCD)                                                                       – Random cell connection tolerant
  – Overvoltage (OV)

1.2        Applications
•      Light Electric Vehicles (LEV): eBikes, eScooters,                                                 •       Battery Backup and UPS Systems
       Pedelec, and Pedal-Assist Bicycles                                                                •       Wireless Base Station Backup Systems
•      Power and Gardening Tools                                                                         •       12-V, 18-V, 24-V, 36-V, and 48-V Battery Packs

1.3        Description
           The bq769x0 family of robust analog front-end (AFE) devices serves as part of a complete
           pack monitoring and protection solution for next-generation, high-power systems, such as light electric
           vehicles, power tools, and uninterruptible power supplies. The bq769x0 is designed with low power in
           mind: Sub-blocks within the IC may be enabled/disabled to control the overall chip current consumption,
           and a SHIP mode provides a simple way to put the pack into an ultra-low power state.

                                                                                    Device Information(1)
                  PART NUMBER                                                                PACKAGE                                                               BODY SIZE (NOM)
    bq76920                                                        TSSOP (20)                                                                                      6.50 mm × 4.40 mm
    bq76930                                                        TSSOP (30)                                                                                      7.80 mm × 4.40 mm
    bq76940                                                        TSSOP (44)                                                                                      11.00 mm × 4.40 mm
    (1) For all available packages, see the orderable addendum at the end of the data sheet.
1.4        Simplified System Diagram

                                                                                                                                                                      PACK +
                                                                                                                               Rf

                                                                                       BAT
                                                                            VC5     REGSRC
                                 Rc
                                            Cc                              VC4     REGOUT
                                                                            VC3       CAP1
                                                                                                 10 kΩ
                                 Rc                                         VC2        TS1
                                            Cc                                                                  1 µF    1 µF   Cf     4.7 µF
                                                                            VC1        SCL       10k
                                 Rc                                         VC0        SDA
                                            Cc
                                                                            SRP        VSS
                                 Rc                                         SRN       CHG
                                            Cc                                                               PUSH-BUTTON FOR BOOT
                                                                            ALERT      DSG
                                                                                                                                               VCC
                                 Rc         Cc
                                                                                                                                               SCL
                                                                                                                                               SDA    Companion
                                       Cc                                                                                                             Controller
                                                                                                                                               GPIO
                                 Rc                                                                    1M
                                                                                                                                               VSS

                                                 0.1 µF   0.1 µF   0.1 µF


                                                   100             100                1M                           1M

                                                          Rsns                                                                                                        PACK–

1




           An IMPORTANT NOTICE at the end of this data sheet addresses availability, warranty, changes, use in safety-critical applications,
           intellectual property matters and other important disclaimers. UNLESS OTHERWISE NOTED, this document contains
           PRODUCTION DATA.
```

## PDF 第 2 页

来源 [BQ76940.pdf PDF p.2](../../sources/BQ76940.pdf#page=2)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                       www.ti.com



                                                                Table of Contents
1   Introduction ............................................... 1           7    Detailed Description ................................... 19
    1.1      Features .............................................. 1            7.1    Overview ............................................ 19
    1.2      Applications ........................................... 1           7.2    Functional Block Diagram ........................... 19
    1.3      Description ............................................ 1           7.3    Feature Description ................................. 20
    1.4      Simplified System Diagram ........................... 1              7.4    Device Functional Modes ........................... 30
2   Revision History ......................................... 2                  7.5    Register Maps ....................................... 32
3   Description (Continued) ................................ 4               8    Application and Implementation .................... 42
4   Device Comparison Table.............................. 4                       8.1    Application Information .............................. 42
5   Pin Configuration and Functions ..................... 5                       8.2    Typical Applications ................................. 43
    5.1      Versions .............................................. 5       9 Power Supply Recommendations .................. 49
    5.2      bq76920 Pin Diagram ................................ 6          10 Layout .................................................... 50
    5.3      bq76930 Pin Diagram ................................ 7               10.1   Layout Guidelines ................................... 50
    5.4      bq76940 Pin Diagram ................................ 9               10.2   Layout Example ..................................... 50
6   Electrical Specifications .............................. 11              11 Device and Documentation Support ............... 53
    6.1      Absolute Maximum Ratings ......................... 11                11.1   Documentation Support ............................. 53
    6.2      ESD Ratings ........................................ 11              11.2   Related Links ........................................ 53
    6.3      Recommended Operating Conditions ............... 11                  11.3   Trademarks.......................................... 53
    6.4      Thermal Information ................................. 13             11.4   Electrostatic Discharge Caution ..................... 53
    6.5      Electrical Characteristics ............................ 13           11.5   Glossary ............................................. 53
    6.6      Timing Requirements ............................... 17          12 Mechanical, Packaging, and Orderable
    6.7      Typical Characteristics .............................. 18          Information .............................................. 53


2 Revision History
                 NOTE: Page numbers for previous revisions may differ from page numbers in the current version.

Changes from Revision E (November 2014) to Revision F                                                                                       Page
    •     Changed bq7693002 From: Product Preview To Production in the Device Comparison Table............................ 4
    •     Added bq7693007 device to the Device Comparison Table .................................................................... 4
    •     Changed table note to group ground reference in bq76930 Pin Functions ................................................... 7
    •     Changed table note to group ground reference in bq76940 Pin Functions ................................................... 9
    •     Changed 10th cell to 11th cell in the Description of pin 29 ................................................................... 10
    •     Changed table formats for online data sheet ................................................................................... 11
    •     Changed Handing Ratings table to ESD Ratings ............................................................................... 11
    •     Changed note for R1 on Figure 7-3............................................................................................... 26
    •     Added more description to the Communications Subsystem section ........................................................ 29
    •     Changed "SHUTA" to "SHUT_A" and "SHUTB" to "SHUT_B" in the SHIP Mode section ................................ 31
    •     Changed CAUTION verbiage (editorial) .......................................................................................... 31
    •     Changed the SHUT_A, SHUT_B bit descriptions in the SYS_CTRL1 (0x04) table ........................................ 35


Changes from Revision D (July 2014) to Revision E                                                                                           Page
    •     Added a note to the Absolute Maximum Ratings table ........................................................................           11
    •     Changed the Handling Ratings ....................................................................................................     11
    •     Added the cross-reference to a new table note at VALERT_IH in Electrical Characteristics .................................            16
    •     Added a new table note ............................................................................................................   16
    •     Added the Typical Characteristics section .......................................................................................     18
    •     Changed the Alert section .........................................................................................................   28
    •     Changed verbiage in Communications Subsystem ............................................................................             29
    •     Deleted the READ/WRITE RSVD register in the Register Map ...............................................................              32
    •     Deleted the READ ONLY register information in the Register Map ..........................................................             32
    •     Changed the reset for Bit 3 in the PROTECT3 register .......................................................................          37
    •     Changed wording in the ADCGAIN bit descriptions ...........................................................................           40
    •     Added Application and Implementation note ....................................................................................        42

2         Revision History                                                                    Copyright © 2013–2015, Texas Instruments Incorporated
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 3 页

来源 [BQ76940.pdf PDF p.3](../../sources/BQ76940.pdf#page=3)

```text
                                                                                                          bq76920, bq76930, bq76940
www.ti.com                                                                               SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



    •   Added Design Requirements ......................................................................................................       45
    •   Added the Detailed Design Procedure ...........................................................................................        46
    •   Changed the Layout Guidelines ...................................................................................................      50
    •   Changed the Layout Example ....................................................................................................        50
    •   Added a Caution ....................................................................................................................   50
    •   Changed the Good Layout figure .................................................................................................       51
    •   Changed the Weak Layout figure ................................................................................................        52


Changes from Revision C (May 2014) to Revision D                                                                                          Page
    •   Changed table reference in Integrated Hardware Protections ................................................................ 25
    •   Changed paragraph 4 verbiage of Integrated Hardware Protections ........................................................ 25


Changes from Revision B (April 2014) to Revision C                                                                                        Page
    •   Changed the documentation format ............................................................................................... 1
    •   Changed some devices from Product Preview to Production Data ............................................................ 4
    •   Changed a bit name in the PROTECT1 register ............................................................................... 36
    •   Changed a bit name in the ADCGAIN2 register ................................................................................ 41


Changes from Revision A (December 2013) to Revision B                                                                                     Page
    •   Changed title of the data manual .................................................................................................. 1
    •   Changed Ordering Information table to ........................................................................................... 4
    •   Changed some devices to Product Preview ...................................................................................... 4
    •   Changed Rf max value in the Recommended Operating Conditions table ................................................. 12
    •   Changed verbiage in mmunications Subsystem ............................................................................... 29
    •   Changed SYS_STAT D6 bit name in the Register Map ....................................................................... 32


Changes from Original (October 2013) to Revision A                                                                                        Page
    •   Changed some devices from Product Preview to Production Data ............................................................ 4
    •   Changed the tINDCELL test condition in Electrical Characteristics .............................................................. 14
    •   Deleted duplicate CELLBAL3 table ............................................................................................... 34
    •   Changed bq76940 with bq783xx Companion Controller IC Schematic ...................................................... 45




Copyright © 2013–2015, Texas Instruments Incorporated                                                                  Revision History         3
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 4 页

来源 [BQ76940.pdf PDF p.4](../../sources/BQ76940.pdf#page=4)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                              www.ti.com



3 Description (Continued)
         The bq76920 device supports up to 5-series cells or typical 18-V packs, the bq76930 handles up to 10-
         series cells or typical 36-V packs, and the bq76940 works for up to 15-series cells or typical 48-V packs. A
         variety of battery chemistries may be managed with these AFEs, including Lithium Ion, Lithium iron
         phosphate, and more.
         Via I2C, a host controller can use the bq769x0 to implement many battery pack management functions,
         such as monitoring (cell voltages, pack current, pack temperatures), protection (controlling
         charge/discharge FETs), and balancing. Integrated A/D converters enable a purely digital readout of
         critical system parameters, with calibration handled in TI’s manufacturing process.

4 Device Comparison Table
          TUBE                TAPE & REEL            CELLS     I2C ADDRESS (7-Bit)   LDO (V)             CRC              PACKAGE
       bq7692000PW           bq7692000PWR                                                                 No
                                                                                        2.5
      bq7692001PW (1)        bq7692001PWR (1)                                                             Yes
                                                                      0x08
      bq7692002PW (1)        bq7692002PWR (1)         3–5                                                 No           20-TSSOP (PW)
       bq7692003PW           bq7692003PWR                                               3.3               Yes
       bq7692006PW           bq7692006PWR                             0x18                                No
      bq7693000DBT           bq7693000DBTR                                                                No
                                                                                        2.5
      bq7693001DBT           bq7693001DBTR                                                                Yes
                                                                      0x08
      bq7693002DBT           bq7693002DBTR                                                                No
                                                     6–10                                                              30-TSSOP (DBT)
      bq7693003DBT           bq7693003DBTR                                                                Yes
                                                                                        3.3
      bq7693006DBT           bq7693006DBTR                                                                No
                                                                      0x18
      bq7693007DBT           bq7693007DBTR                                                                Yes
      bq7694000DBT           bq7694000DBTR                                                                No
                                                                                        2.5
      bq7694001DBT           bq7694001DBTR                                                                Yes
                                                                      0x08
      bq7694002DBT           bq7694002DBTR           9–15                                                 No           44-TSSOP (DBT)
      bq7694003DBT           bq7694003DBTR                                              3.3               Yes
      bq7694006DBT           bq7694006DBTR                            0x18                                No

(1)   Product Preview only


         Texas Instruments pre-configures the bq769x0 devices for a specific I2C address, LDO voltage, and more.
         These settings are permanently stored in EEPROM and cannot be further modified.
         Contact Texas Instruments for other options not listed above, as well as any options noted as “Product
         Preview only.”




4        Device Comparison Table                                                     Copyright © 2013–2015, Texas Instruments Incorporated
                                                    Submit Documentation Feedback
                                            Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 5 页

来源 [BQ76940.pdf PDF p.5](../../sources/BQ76940.pdf#page=5)

```text
                                                                                                           bq76920, bq76930, bq76940
www.ti.com                                                                                SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



5 Pin Configuration and Functions
5.1       Versions

   DSG    1                        20   ALERT           DSG     1                 30   ALERT         DSG     1                    44   ALERT
   CHG    2                        19   SRN             CHG     2                 29   SRN           CHG     2                    43   SRN
   VSS    3                        18   SRP              VSS    3                 28   SRP            VSS    3                    42   SRP
   SDA    4                        17   VC0             SDA     4                 27   VC0           SDA     4                    41   VC0
    SCL   5     bq76920            16   VC1              SCL    5                 26   VC1            SCL    5                    40   VC1
                                        VC2                                            VC2
    TS1   6     20-TSSOP           15                    TS1    6                 25                  TS1    6                    39   VC2
  CAP 1   7                        14   VC3             CAP1    7                 24   VC3          CAP1     7                    38   VC3
REGOUT    8                        13   VC4       REGOUT        8
                                                                     bq76930      23   VC4        REGOUT     8                    37   VC4
REGSRC    9                        12   VC5       REGSRC        9    30-TSSOP     22   VC5        REGSRC     9                    36   VC5
   BAT    10                       11   NC              VC5x    10                21   VC5B          VC5x   10                    35   VC5B
                                                         NC     11                20   VC6            NC    11   bq76940          34   VC6
                                                         NC     12                19   VC7            NC    12                    33   VC7
                                                                                                            13
                                                                                                                 44-TSSOP
                                                         TS2    13                18   VC8            TS2                         32   VC8
                                                        CAP2    14                17   VC9          CAP2    14                    31   VC9
                                                        BAT     15                16   VC10         VC10x   15                    30   VC10
                                                                                                      NC    16                    29   VC10B
                                                                                                      NC    17                    28   VC11
                                                                                                      TS3   18                    27   VC12
                                                                                                    CAP3    19                    26   VC13
                                                                                                     BAT    20                    25   VC14
                                                                                                      NC    21                    24   VC15
                                                                                                      NC    22                    23   NC



                                                               Figure 5-1. Pin Versions

          bq76920: 3–5 Series Cells (20-TSSOP)
          • 6.5 mm x 4.4 mm x 1.2 mm
          bq76930: 6–10 Series Cells (30-TSSOP)
          • 7.8 mm x 4.4 mm x 1.2 mm
          bq76940: 9–15 Series Cells (44-TSSOP)
          • 11.3 mm x 4.4 mm x 1.2 mm




Copyright © 2013–2015, Texas Instruments Incorporated                                                Pin Configuration and Functions         5
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 6 页

来源 [BQ76940.pdf PDF p.6](../../sources/BQ76940.pdf#page=6)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                     www.ti.com



5.2     bq76920 Pin Diagram


                                                DSG     1                             20    ALERT
                                                CHG     2                             19    SRN
                                                 VSS    3                             18    SRP
                                                 SDA    4                             17    VC0
                                                 SCL    5       bq76920               16    VC1
                                                                                            VC2
                                                 TS1    6       20-TSSOP              15
                                               CAP 1    7                             14    VC3
                                             REGOUT     8                             13    VC4
                                             REGSRC     9                             12    VC5
                                                 BAT    10                            11    NC



5.2.1    bq76920 Pin Map


                                                       bq76920 Pin Functions
                      PIN             NAME             TYPE                             DESCRIPTION
                       1               DSG               O        Discharge FET driver
                       2               CHG               O        Charge FET driver
                       3               VSS               —        Chip VSS
                       4               SDA               I/O      I2C communication to the host controller
                       5               SCL                  I     I2C communication to the host controller
                       6               TS1                  I     Thermistor #1 positive terminal (1)
                       7              CAP1               O        Capacitor to VSS
                       8             REGOUT                 P     Output LDO
                       9             REGSRC                 I     Input source for output LDO
                       10              BAT                  P     Battery (top-most) terminal
                       11               NC               —        No connect
                       12              VC5                  I     Sense voltage for 5th cell positive terminal
                       13              VC4                  I     Sense voltage for 4th cell positive terminal
                       14              VC3                  I     Sense voltage for 3rd cell positive terminal
                       15              VC2                  I     Sense voltage for 2nd cell positive terminal
                       16              VC1                  I     Sense voltage for 1st cell positive terminal
                       17              VC0                  I     Sense voltage for 1st cell negative terminal
                       18              SRP                  I     Negative current sense (nearest VSS)
                       19              SRN                  I     Positive current sense
                       20             ALERT              I/O      Alert output and override input
                (1)   If not used, pull down to VSS with a 10-kΩ nominal resistor.




6       Pin Configuration and Functions                                                     Copyright © 2013–2015, Texas Instruments Incorporated
                                                      Submit Documentation Feedback
                                              Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 7 页

来源 [BQ76940.pdf PDF p.7](../../sources/BQ76940.pdf#page=7)

```text
                                                                                                             bq76920, bq76930, bq76940
www.ti.com                                                                                  SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



5.3     bq76930 Pin Diagram


                                                   DSG    1                            30     ALERT
                                                   CHG    2                            29     SRN
                                                   VSS    3                            28     SRP
                                                   SDA    4                            27     VC0
                                                    SCL   5                            26     VC1
                                                    TS1   6                            25     VC2
                                                  CAP1    7                            24     VC3
                                                REGOUT    8
                                                                   bq76930             23     VC4
                                                REGSRC    9        30-TSSOP            22     VC5
                                                  VC5x    10                           21     VC5B
                                                    NC    11                           20     VC6
                                                    NC    12                           19     VC7
                                                    TS2   13                           18     VC8
                                                  CAP2    14                           17     VC9
                                                   BAT    15                           16     VC10



5.3.1     bq76930 Pin Map


                                                          bq76930 Pin Functions
                        PIN              NAME             TYPE                              DESCRIPTION
                         1                DSG                 O     Discharge FET driver
                         2                CHG                 O     Charge FET driver
                         3                VSS                 —     Chip VSS
                         4                SDA              I/O      I2C communication to the host controller
                         5                SCL                  I    I2C communication to the host controller
                         6                TS1                  I    Thermistor #1 positive terminal (1)
                         7               CAP1                 O     Capacitor to VSS
                         8             REGOUT                  P    Output LDO
                         9             REGSRC                  I    Input source for output LDO
                         10              VC5X                  P    Thermistor #2 negative terminal
                         11                NC                 —     No connect (short to CAP2)
                         12                NC                 —     No connect (short to CAP2)
                         13               TS2                  I    Thermistor #2 positive terminal (1)
                         14              CAP2                 O     Capacitor to VC5X
                         15               BAT                  P    Battery (top-most) terminal
                         16              VC10                  I    Sense voltage for 10th cell positive terminal
                         17               VC9                  I    Sense voltage for 9th cell positive terminal
                         18               VC8                  I    Sense voltage for 8th cell positive terminal
                         19               VC7                  I    Sense voltage for 7th cell positive terminal
                         20               VC6                  I    Sense voltage for 6th cell positive terminal
                         21              VC5B                  I    Sense voltage for 6th cell negative terminal
                         22               VC5                  I    Sense voltage for 5th cell positive terminal
                         23               VC4                  I    Sense voltage for 4th cell positive terminal

                 (1)   If not used, pull down to group ground reference (VSS for TS1 and VC5X for TS2) with a 10-kΩ
                       nominal resistor.
Copyright © 2013–2015, Texas Instruments Incorporated                                                     Pin Configuration and Functions   7
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 8 页

来源 [BQ76940.pdf PDF p.8](../../sources/BQ76940.pdf#page=8)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                www.ti.com



                                            bq76930 Pin Functions (continued)
                     PIN            NAME            TYPE                           DESCRIPTION
                     24              VC3              I       Sense voltage for 3rd cell positive terminal
                     25              VC2              I       Sense voltage for 2nd cell positive terminal
                     26              VC1              I       Sense voltage for 1st cell positive terminal
                     27              VC0              I       Sense voltage for 1st cell negative terminal
                     28              SRP              I       Negative current sense (nearest VSS)
                     29              SRN              I       Positive current sense
                     30             ALERT            I/O      Alert output and override input




8      Pin Configuration and Functions                                                 Copyright © 2013–2015, Texas Instruments Incorporated
                                                   Submit Documentation Feedback
                                           Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 9 页

来源 [BQ76940.pdf PDF p.9](../../sources/BQ76940.pdf#page=9)

```text
                                                                                                                 bq76920, bq76930, bq76940
www.ti.com                                                                                       SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



5.4     bq76940 Pin Diagram

                                                   DSG         1                            44     ALERT
                                                   CHG         2                            43     SRN
                                                   VSS         3                            42     SRP
                                                   SDA         4                            41     VC0
                                                    SCL        5                            40     VC1
                                                   TS1         6                            39     VC2
                                                  CAP1         7                            38     VC3
                                              REGOUT           8                            37     VC4
                                               REGSRC          9                            36     VC5
                                                  VC5x        10                            35     VC5B
                                                    NC        11     bq76940                34     VC6
                                                    NC        12                            33     VC7
                                                              13
                                                                     44-TSSOP
                                                   TS2                                      32     VC8
                                                  CAP2        14                            31     VC9
                                                 VC10x        15                            30     VC10
                                                    NC        16                            29     VC10B
                                                    NC        17                            28     VC11
                                                   TS3        18                            27     VC12
                                                  CAP3        19                            26     VC13
                                                   BAT        20                            25     VC14
                                                    NC        21                            24     VC15
                                                    NC        22                            23     NC



5.4.1     bq76940 Pin Map


                                                              bq76940 Pin Functions
                       PIN          NAME                TYPE                                DESCRIPTION
                        1            DSG                 O         Discharge FET driver
                        2            CHG                 O         Charge FET driver
                        3            VSS                 —         Chip VSS
                        4            SDA                I/O        I2C communication to the host controller
                        5            SCL                  I        I2C communication to the host controller
                        6             TS1                 I        Thermistor #1 positive terminal (1)
                        7            CAP1                O         Capacitor to VSS
                        8          REGOUT                P         Output LDO
                        9          REGSRC                 I        Input source for output LDO
                       10            VC5X                P         Thermistor #2 negative terminal
                       11             NC                 —         No connect (short to CAP2)
                       12             NC                 —         No connect (short to CAP2)
                       13             TS2                 I        Thermistor #2 positive terminal (1)
                       14            CAP2                O         Capacitor to VC5X
                       15           VC10X                P         Thermistor #3 negative terminal

                 (1)   If not used, pull down to group ground reference (VSS for TS1, VC5X for TS2, and VC10X for TS3)
                       with a 10-kΩ nominal resistor.
Copyright © 2013–2015, Texas Instruments Incorporated                                                         Pin Configuration and Functions   9
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 10 页

来源 [BQ76940.pdf PDF p.10](../../sources/BQ76940.pdf#page=10)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                 www.ti.com



                                          bq76940 Pin Functions (continued)
                   PIN          NAME         TYPE                              DESCRIPTION
                    16           NC           —       No connect (short to CAP3)
                    17           NC           —       No connect (short to CAP3)
                    18           TS3           I      Thermistor #3 positive terminal (1)
                    19          CAP3          O       Capacitor to VC10X
                    20           BAT           P      Battery (top-most) terminal
                    21           NC           —       No connect
                    22           NC           —       No connect
                    23           NC           —       No connect
                    24          VC15           I      Sense voltage for 15th cell positive terminal
                    25          VC14           I      Sense voltage for 14th cell positive terminal
                    26          VC13           I      Sense voltage for 13th cell positive terminal
                    27          VC12           I      Sense voltage for 12th cell positive terminal
                    28          VC11           I      Sense voltage for 11th cell positive terminal
                    29         VC10B           I      Sense voltage for 11th cell negative terminal
                    30          VC10           I      Sense voltage for 10th cell positive terminal
                    31           VC9           I      Sense voltage for 9th cell positive terminal
                    32           VC8           I      Sense voltage for 8th cell positive terminal
                    33           VC7           I      Sense voltage for 7th cell positive terminal
                    34           VC6           I      Sense voltage for 6th cell positive terminal
                    35          VC5B           I      Sense voltage for 6th cell negative terminal
                    36           VC5           I      Sense voltage for 5th cell positive terminal
                    37           VC4           I      Sense voltage for 4th cell positive terminal
                    38           VC3           I      Sense voltage for 3rd cell positive terminal
                    39           VC2           I      Sense voltage for 2nd cell positive terminal
                    40           VC1           I      Sense voltage for 1st cell positive terminal
                    41           VC0           I      Sense voltage for 1st cell negative terminal
                    42           SRP           I      Negative current sense (nearest VSS)
                    43           SRN           I      Positive current sense
                    44         ALERT          I/O     Alert output and override input




10     Pin Configuration and Functions                                                  Copyright © 2013–2015, Texas Instruments Incorporated
                                                 Submit Documentation Feedback
                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 11 页

来源 [BQ76940.pdf PDF p.11](../../sources/BQ76940.pdf#page=11)

```text
                                                                                                          bq76920, bq76930, bq76940
www.ti.com                                                                               SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



6 Electrical Specifications

6.1      Absolute Maximum Ratings
Over-operating free-air temperature range (unless otherwise noted)
                                                                                                              MIN           MAX       UNIT
                               (BAT–VSS)                                                   bq76920
          Supply voltage
VBAT                           (BAT–VC5x), (VC5x–VSS)                                      bq76930            –0.3             36        V
          range
                               (BAT–VC10x), (VC10x–VC5x), (VC5x–VSS)                       bq76940
                               (VCn–VSS) where n = 1..5                                    bq76920
                               (VCn–VSS) where n = 1..5, (VCn-VC5x) where n =
                                                                                           bq76930
                               6..10                                                                          –0.3       (n × 7.2)       V
                               (VCn–VSS) where n = 1..5, (VCn–VC5x) where n =
                                                                                           bq76940
                               6..10, (VCn–VC10x) where n = 11..15
                               Cell input pins, differential (VCn–VCn–1) where n = 1..15/10/5
                                                                                                              –0.3              9        V
                               (bq76940/bq76930/bq76920, respectively)
          Input voltage
VI                             SRN, SRP, SCL, SDA
          range
                               (VC0–VSS), (CAP1–VSS), (TS1–VSS) (1)                        bq76920
                               (VC0–VSS), (VC5b–VC5x), (CAP2–VC5x),
                                                                                           bq76930            –0.3            3.6
                               (CAP1–VSS), (TS2–VC5x), (TS1–VSS) (1)
                                                                                                                                         V
                               (VC0–VSS), (VC5b–VC5x), (VC10b–VC10x),
                               (CAP3–VC10x), (CAP2–VC5x), (CAP1–VSS),                      bq76940
                               (TS3–VC10x), (TS2–VC5x), (TS1–VSS) (1)
                               REGSRC                                                                         –0.3             36
                               REGOUT, ALERT                                                                  –0.3            3.6
          Output voltage
VO                             DSG                                                                            –0.3             20        V
          range
                               CHG                                                                            –0.3 VCHGCLAMP
                                                                                           bq76920                 70                    mA
ICB       Cell balancing current (per cell)                                                bq76930,
                                                                                                                     5                   mA
                                                                                           bq76940
IDSG      Discharge pin input current when disabled (measured into terminal)                                         7                   mA
          Storage temperature range                                                                           –65             150
TSTG                                                                                                                                     °C
          Lead temperature (soldering, 10 s)                                                                                  300
(1)   The Absolute Maximum Ratings for (TS1–VSS) apply after the device completes POR and should be observed after tBOOTREADY
      (10 ms), following the application of the boot signal on TS1. Prior to completion of POR, TS1 should not exceed 5 V.

6.2      ESD Ratings
                                                                                                                         VALUE        UNIT
                                                                           (1)
          Electrostatic     Human body model (HBM) ESD stress voltage                                                          ±2        kV
V(ESD)
          discharge         Charged device model (CDM) ESD stress voltage (2)                                               ±500         V
(1)   JEDEC document JEP155 states that 500-V HBM allows safe manufacturing with a standard ESD control process.
(2)   JEDEC document JEP157 states that 250-V CDM allows safe manufacturing with a standard ESD control process.

6.3      Recommended Operating Conditions
Over-operating free-air temperature range (unless otherwise noted). See Section 8.1.1 for more information on cell
configurations. All voltages are relative to VSS, except "Cell input differential."
                                                                                                 MIN         TYP              MAX        UNIT
                                   (BAT–VSS)                                bq76920
           Supply voltage          (BAT–VC5x), (VC5x–VSS)                   bq76930
VBAT                                                                                                  6                          25       V
           range
                                   (BAT–VC10x), (VC10x–VC5x),
                                                                            bq76940
                                   (VC5x–VSS)




Copyright © 2013–2015, Texas Instruments Incorporated                                                        Electrical Specifications          11
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 12 页

来源 [BQ76940.pdf PDF p.12](../../sources/BQ76940.pdf#page=12)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                      www.ti.com



Recommended Operating Conditions (continued)
Over-operating free-air temperature range (unless otherwise noted). See Section 8.1.1 for more information on cell
configurations. All voltages are relative to VSS, except "Cell input differential."
                                                                                                 MIN         TYP             MAX        UNIT
                                     Cell input pins, differential (VCn–VCn–1) where n =
                                     1..15/10/5 (bq76940/bq76930/bq76920, respectively),           2                             5       V
                                     in-use cells only
                                     (VCn–VSS) where n = 1..5              bq76920
                                     (VCn–VSS) where n = 1..5,
                                                                           bq76930
                                     (VCn–VC5x) where n = 6..10
                                                                                                   0                         5×n         V
                                     (VCn–VSS) where n = 1..5,
                                     (VCn–VC5x) where n = 6..10,           bq76940
                                     (VCn–VC10x) where n = 11..15
                                     SRP
                                     (VC0–VSS)                             bq76920
VIN        Input voltage range
                                     (VC0–VSS), (VC5b–VC5x)                bq76930               –10                            10      mV
                                     (VC0–VSS), (VC5b–VC5x),
                                                                           bq76940
                                     (VC10b–VC10x)
                                     SRN                                                       –200                            200      mV
                                     SCL, SDA
                                     (TS1–VSS)                             bq76920
                                     (TS1–VSS), (TS2–VC5x)                 bq76930                 0                           3.6
                                                                                                                                         V
                                     (TS1–VSS), (TS2–VC5x),
                                                                           bq76940
                                     (TS3–VC10x)
                                     REGSRC                                                        6                            25
                                     CHG, DSG                                                      0                            16       V
                                     REGOUT, ALERT
           Output voltage            (CAP1–VSS)                            bq76920
VOUT
           range                                                                                   0                           3.6       V
                                     (CAP1–VSS), (CAP2–VC5x)               bq76930
                                     (CAP1–VSS), (CAP2–VC5x),
                                                                           bq76940
                                     (CAP3–VC10x)
           Cell balancing                                 bq76920                                  0                            50      mA
ICB        current (internal, per
           cell)                                     bq76930, bq76940                              0                             5      mA

           External cell input                            bq76920                                 40          100               1K       Ω
RC
           resistance                                bq76930, bq76940                            500           1K               1K       Ω
CC         External cell input capacitance                                                       0.1             1              10       µF
Rf         External supply filter resistance                                                      40          100               1K       Ω
Cf         External supply filter capacitance                                                      1           10               40       µF
RFILT      Sense resistor filter resistance                                                      100           1K                        Ω
RALERT     ALERT pin to VSS resistor                                                                           1M                        Ω
CL         REGOUT loading capacitance                                                              1           4.7                       µF
CCAP       REGSRC, CAP1, CAP2, and CAP3 output capacitance                                         1                                     µF
RTS        External thermistor nominal resistance (103AT) at 25ºC                                10K                                     Ω
TOPR       Operating free-air temperature                                                        –40                            85       °C




12       Electrical Specifications                                                         Copyright © 2013–2015, Texas Instruments Incorporated
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 13 页

来源 [BQ76940.pdf PDF p.13](../../sources/BQ76940.pdf#page=13)

```text
                                                                                                         bq76920, bq76930, bq76940
www.ti.com                                                                              SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



6.4       Thermal Information
Over-operating free-air temperature range (unless otherwise noted)
                                                                                             TSSOP
                     THERMAL METRIC (1)                              bq76920xy            bq76930xy             bq76940xy                 UNIT
                                                                    20 PINS (PW)         30 PINS (DBT)         44 PINS (DBT)
RθJA, High K Junction-to-ambient thermal resistance (2)                 93.7                  86.5                     70.1
RθJC(top)     Junction-to-case(top) thermal resistance (3)              28.7                  19.4                     17.5
                                                        (4)
RθJB          Junction-to-board thermal resistance                      44.6                  41.3                     33.9
                                                                                                                                          °C/W
ψJT           Junction-to-top characterization parameter (5)            1.3                    0.5                     0.5
ψJB           Junction-to-board characterization parameter (6)          44.1                  40.6                     33.4
                                                              (7)
RθJC(bottom) Junction-to-case(bottom) thermal resistance                n/a                    n/a                     n/a
(1)   For more information about traditional and new thermal metrics, see the Semiconductor and IC Package Thermal Metrics application
      report (SPRA953).
(2)   The junction-to-ambient thermal resistance under natural convection is obtained in a simulation on a JEDEC-standard, high-K board, as
      specified in JESD51-7, in an environment described in JESD51-2a.
(3)   The junction-to-case (top) thermal resistance is obtained by simulating a cold plate test on the package top. No specific JEDEC-
      standard test exists, but a close description can be found in the ANSI SEMI standard G30-88.
(4)   The junction-to-board thermal resistance is obtained by simulating in an environment with a ring cold plate fixture to control the PCB
      temperature, as described in JESD51-8.
(5)   The junction-to-top characterization parameter, ψJT, estimates the junction temperature of a device in a real system and is extracted
      from the simulation data for obtaining RθJA, using a procedure described in JESD51-2a (sections 6 and 7).
(6)   The junction-to-board characterization parameter, ψJB, estimates the junction temperature of a device in a real system and is extracted
      from the simulation data for obtaining RθJA, using a procedure described in JESD51-2a (sections 6 and 7).
(7)   The junction-to-case (bottom) thermal resistance is obtained by simulating a cold plate test on the exposed (power) pad. No specific
      JEDEC standard test exists, but a close description can be found in the ANSI SEMI standard G30-88.
      Spacer

6.5       Electrical Characteristics
Typical conditions are measured at 25ºC with nominal BAT voltages of 18 V (bq76920), 36 V (bq76930), or 48 V (bq76940)
with VCELL = 4 V. Min and max values include full recommended operating condition temperature range from –40ºC to +85ºC.
Certain characteristics may be shown at different voltage or temperature ranges, as clarified in the Test Condition sections.
  PARAMETER               DESCRIPTION                         TEST CONDITION                      MIN          TYP            MAX     UNIT
SUPPLY CURRENTS
                    NORMAL mode: ADC off,
                                                                                                                 40            60
                    CC off
                    NORMAL mode: ADC on,
                                                                                                                 60            90
                    CC off                        Sum of ICC_BAT and ICC_REGSRC
IDD
                    NORMAL mode: ADC off,         currents
                                                                                                                110           165
                    CC on
                    NORMAL mode: ADC on,
                                                                                                                130           195
                    CC on                                                                                                                 µA
                    NORMAL mode: ADC off                                                                         30            45
ICC_BAT                                           Into BAT pin
                    NORMAL mode: ADC on                                                                          50            75
                    NORMAL mode: CC off                                                                          10            15
ICC_REGSRC                                        Into REGSRC pin
                    NORMAL mode: CC on                                                                           80           120
                                                  Device in full shutdown, only
ISHIP               SHIP/SHUTDOWN mode                                                                           0.6           1.8
                                                  VSTUP/BG and BOOT detector on




Copyright © 2013–2015, Texas Instruments Incorporated                                                         Electrical Specifications          13
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 14 页

来源 [BQ76940.pdf PDF p.14](../../sources/BQ76940.pdf#page=14)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                      www.ti.com



Electrical Characteristics (continued)
Typical conditions are measured at 25ºC with nominal BAT voltages of 18 V (bq76920), 36 V (bq76930), or 48 V (bq76940)
with VCELL = 4 V. Min and max values include full recommended operating condition temperature range from –40ºC to +85ºC.
Certain characteristics may be shown at different voltage or temperature ranges, as clarified in the Test Condition sections.
  PARAMETER                DESCRIPTION                       TEST CONDITION                       MIN           TYP          MAX       UNIT
LEAKAGE AND OFFSET CURRENTS
                     NORMAL mode supply
dINOM                                                                                              –5           ±2.5             5
                     current offset               Measured into VC5x (bq76930,
                     SHIP mode supply             bq76940) and VC10x (bq76940)
dISHIP                                                                                           –1.0           ±0.1           1.0
                     current offset
                     Supply current when          Measured into VC5x (bq76930,
dIALERT                                                                                                           15            25      µA
                     ALERT active                 bq76940) or added to BAT (bq76920)
                                                  Measured into VC0–VC15 except VC5,
                     Cell measurement input                                                      –0.3           ±0.1           0.3
dICELL                                            VC10, VC15
                     current
                                                  Measured into VC5, VC10, VC15                                                0.5
ILKG                 Terminal input leakage                                                                                      1
INTERNAL POWER CONTROL (STARTUP and SHUTDOWN)
VPORA                Analog POR threshold         See Note (1)                                       4                           5       V
VSHUT                Shutdown voltage             See Note (1)                                                                 3.6       V
                     Time delay after boot
                                                  Delay after boot sequence when I2C
tI2CSTARTUP          signal on TS1 before I2C                                                                      1                    ms
                                                  communication is allowed
                     communications allowed
                                                  Delay after boot signal when device has
tBOOTREADY           Device boot startup delay                                                                                  10      ms
                                                  completed full boot-up sequence
                     Thermal shutdown
TSHUTD                                                                                                           100          150       °C
                     voltage
MEASUREMENT SCHEDULE
                     Cell voltage measurement
tVCELL                                        bq76920, bq76930, bq76940                                          250
                     interval
                     Individual cell              Per cell, balancing off                                         50
tINDCELL
                     measurement time             Per cell, balancing on                                        12.5
                     Cell balancing relaxation
tCB_RELAX            time before cell voltage                                                                   12.5                    ms
                     measured
                     Temperature
                                                  Measurement duration for temperature
tTEMP_DEC            measurement decimation                                                                     12.5
                                                  reading
                     time
                     Pack voltage calculation
tBAT                                                                                                             250
                     interval
                     Temperature                  Period of measurement of either
tTEMP                                                                                                              2                     s
                     measurement interval         TS1/TS2/TS3 or internal die temp
14-BIT ADC FOR CELL VOLTAGE AND TEMPERATURE MEASUREMENT
                     ADC measurement              VCELL measurements                                 2                           5       V
ADCRANGE             recommend operation
                     range                        TS/Temp measurements                             0.3                           3       V
ADCLSB               ADC LSB value                                                                               382                    µV
ADC25A                                            VCELL = 3.6 – 4.3 V                             –40            ±10            40
                     ADC cell voltage
ADC25B                                            VCELL = 3.2 – 4.6 V                             –40            ±15            40
                     accuracy
ADC25C                                            VCELL = 2.0 – 5.0 V                             –50            ±25            50
                     ADC cell voltage                                                                                                   mV
ADC60                accuracy temperature drift TA = 0ºC to 60ºC                                  –10                           10
                     adder from 25°C
                     ADC thermistor
ADCTS                                             Input from 0.3 to 3.0 V, BAT > 7 V              –35                           35
                     measurement accuracy
(1)    Measured at BAT pin, rising.



14        Electrical Specifications                                                         Copyright © 2013–2015, Texas Instruments Incorporated
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 15 页

来源 [BQ76940.pdf PDF p.15](../../sources/BQ76940.pdf#page=15)

```text
                                                                                                             bq76920, bq76930, bq76940
www.ti.com                                                                                    SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



Electrical Characteristics (continued)
Typical conditions are measured at 25ºC with nominal BAT voltages of 18 V (bq76920), 36 V (bq76930), or 48 V (bq76940)
with VCELL = 4 V. Min and max values include full recommended operating condition temperature range from –40ºC to +85ºC.
Certain characteristics may be shown at different voltage or temperature ranges, as clarified in the Test Condition sections.
  PARAMETER               DESCRIPTION                         TEST CONDITION                          MIN         TYP         MAX       UNIT
16-BIT CC FOR PACK CURRENT MEASUREMENT
CCRANGE             CC input voltage range                                                           –200                      200          mV
CCFSR               CC full scale range                                                              –270                      270          mV
CCLSB               CC LSB value                  CC running constantly                                           8.44                      µV
tCCREAD             Conversion time               Single conversion                                               250                       ms
                                                  16-bit, best fit over input voltage range
CCINL               Integral nonlinearity                                                                          ±2          ± 40     LSB
                                                  ± 200 mV
CCOFFSET            Offset error                                                                                   ±1           ±3      LSB
CCGAIN              Gain error                    Over input voltage range                                     ± 0.5%       ± 1.5%      FSR
CCGAINDRIFT         Gain error drift              Over input voltage range                                                     150    PPM / °C
CCRIN               Effective input resistance                                                                     2.5                      MΩ
THERMISTOR BIAS
RTS                 Pull-up resistance            TA = 25°C                                          9.85          10        10.15          kΩ
                    Pull-up resistance across
RTSDRIFT                                          TA = –40°C to 85°C                                   9.7                     10.3         kΩ
                    temp
DIETEMP
VDIETEMP25          Die temperature voltage       TA = 25°C                                                       1.20                      V
                    Die temperature voltage
VDIETEMPDRIFT                                                                                                     –4.2                 mV/°C
                    drift
INTEGRATED HARDWARE PROTECTIONS
OVRANGE             OV threshold range                                                             0x2008                  0x2FF8       ADC
UVRANGE             UV threshold range                                                             0x1000                  0x1FF0       ADC
                    OV and UV threshold step
OVUVSTEP                                                                                                           16                   LSB
                    size
                    UV minimum value to           Below UVMINQUAL, cell is shorted
UVMINQUAL                                                                                                      0x0518                   ADC
                    qualify                       (unused)
                                                  OV delay = 1 s                                       0.7           1         1.75
                                                  OV delay = 2 s                                       1.6           2         2.75
OVDELAY             OV delay timer options
                                                  OV delay = 4 s                                       3.5           4            5
                                                  OV delay = 8 s                                        7            8           10
                                                                                                                                            s
                                                  UV delay = 1 s                                       0.7           1         1.75
                                                  UV delay = 4 s                                       3.5           4            5
UVDELAY             UV delay timer options
                                                  UV delay = 8 s                                        7            8           10
                                                  UV delay = 16 s                                      14          16            20
                                                                                  (2)
OCDRANGE            OCD threshold options         Measured across (SRP–SRN)                             8                      100          mV
                                                  RSNS = 0                                                        2.78                      mV
OCDSTEP             OCD threshold step size
                                                  RSNS = 1                                                        5.56                      mV
OCDDELAY            OCD delay options             See Note (3)                                          8                     1280          ms
                                                                                  (2)
SCDRANGE            SCD threshold options         Measured across (SRP–SRN)                            22                      200          mV
                                                  RSNS = 0                                                        11.1                      mV
SCDSTEP             SCD threshold step size
                                                  RSNS = 1                                                        22.2                      mV
                                                                                                       35          70          105          µs
                                                                                                       50         100          150          µs
SCDDELAY            SCD delay options
                                                                                                      140         200          260          µs
                                                                                                      280         400          520          µs
(2)   Values indicate nominal thresholds only. For Min and Max variation, apply OCOFFSET and OCSCALERR.
(3)   Values indicate nominal thresholds only. For Min and Max variation, apply tPROTACC.
Copyright © 2013–2015, Texas Instruments Incorporated                                                           Electrical Specifications        15
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 16 页

来源 [BQ76940.pdf PDF p.16](../../sources/BQ76940.pdf#page=16)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                      www.ti.com



Electrical Characteristics (continued)
Typical conditions are measured at 25ºC with nominal BAT voltages of 18 V (bq76920), 36 V (bq76930), or 48 V (bq76940)
with VCELL = 4 V. Min and max values include full recommended operating condition temperature range from –40ºC to +85ºC.
Certain characteristics may be shown at different voltage or temperature ranges, as clarified in the Test Condition sections.
  PARAMETER                DESCRIPTION                      TEST CONDITION                        MIN           TYP          MAX       UNIT
tPROTACC             Delay accuracy for OCD                                                     –20%                         20%
                     OCD and SCD voltage
OCOFFSET                                                                                         –2.5                          2.5      mV
                     offset
                     OCD and SCD scale
OCSCALEERR                                                                                      –10%                         10%
                     accuracy
CHARGE AND DISCHARGE DRIVERS
                                                  REGSRC ≥ 12 V with load resistance of
                                                                                                   10             12            14       V
                                                  10 MΩ
VFETON               CHG and DSG on
                                                  REGSRC < 12 V with load resistance of     REGSRC         REGSRC
                                                                                                                        REGSRC           V
                                                  10 MΩ                                         –2             –1
                                                  CHG/DSG driving an equivalent load
                     CHG and DSG ON rise
tFET_ON                                           capacitance of 10 nF, measured from                            200          250        µs
                     time
                                                  10% to 90% of VFETON
                                                  DSG driving an equivalent load
                     DSG pull-down OFF fall
tDSG_OFF                                          capacitance of 10 nF, measured from                             60            90       µs
                     time
                                                  90% to 10%
                     CHG pull-down OFF
RCHG_OFF                                          When CHG disabled, CHG held at 12 V             750          1000          1250       kΩ
                     resistance to VSS
                     DSG pull-down OFF
RDSG_OFF                                          When DSG disabled, DSG held at 12 V            1.75           2.50          4.25      kΩ
                     resistance to VSS
VLOAD_DETECT         Load detection threshold                                                      0.4           0.7           1.0       V
                                                  If the CHG pin externally pulled high
                                                  (through PACK–, if load applied),
VCHG_CLAMP           CHG clamp voltage                                                             18             20            22       V
                                                  500 µA max sink current into CHG pin.
                                                  With CHG_ON bit cleared.
ALERT PIN
                     ALERT output voltage                                                  REGOUT x
VALERT_OH                                         IOL = 1 mA                                                                             V
                     high                                                                      0.75
                                                                                                                        REGOUT
VALERT_OL            ALERT output voltage low Unloaded                                                                                   V
                                                                                                                          x 0.25
                                                  ALERT externally forced high when
VALERT_IH            ALERT input high                                                                1                         1.5       V
                                                  internally driven low. See note (4).
                     ALERT pin weak
                                              Measured into ALERT pin with ALERT
RALERT_PD            pulldown resistance when                                                      0.8           2.5             8      MΩ
                                              = REGOUT
                     driven low
CELL BALANCING DRIVER
                     Internal cell balancing
RDSFET                                            VCELL = 3.6 V                                      1             5            10       Ω
                     driver resistance
                     Cell balancing duty cycle
XBAL                                              Every 250 ms                                                  70%
                     when enabled
EXTERNAL REGULATOR
                     External LDO voltage         Nominal values, TI factory programmed,         2.45           2.50          2.55       V
VEXTLDO
                     options                      unloaded, across temp                          3.20           3.30          3.40       V
                                                  REGSRC pin stepped from 6 to 25 V,
VEXTLDO_LN           Line regulation                                                                                          100       mV
                                                  with 10 mA load, in 100 µs
VEXTLDO_LD           Load regulation              IREGOUT = 0 mA to 10 mA                        –4%                           4%
                                                  REGOUT = 10 mA DC, 2.5-V version                 2.4                                   V
                     External LDO minimum         REGOUT = 20 mA DC, 2.5-V version                 2.3                                   V
VEXTLDO_DC
                     voltage under DC load        REGOUT = 10 mA DC, 3.3-V version               3.15                                    V
                                                  REGOUT = 20 mA DC, 3.3-V version               3.05                                    V

(4)   MIN specifies the threshold below which the device will never register that an external alert has occurred. MAX specifies the minimum
      threshold above which the device will always register that an external alert has occurred.
16        Electrical Specifications                                                         Copyright © 2013–2015, Texas Instruments Incorporated
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 17 页

来源 [BQ76940.pdf PDF p.17](../../sources/BQ76940.pdf#page=17)

```text
                                                                                                           bq76920, bq76930, bq76940
www.ti.com                                                                                SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



Electrical Characteristics (continued)
Typical conditions are measured at 25ºC with nominal BAT voltages of 18 V (bq76920), 36 V (bq76930), or 48 V (bq76940)
with VCELL = 4 V. Min and max values include full recommended operating condition temperature range from –40ºC to +85ºC.
Certain characteristics may be shown at different voltage or temperature ranges, as clarified in the Test Condition sections.
     PARAMETER            DESCRIPTION                       TEST CONDITION                        MIN          TYP          MAX       UNIT
IEXTLDO_LIMIT       External LDO current limit REGOUT = 0 V                                           30           38          45         mA
BOOT DETECTOR
                                                  Measured at TS1 pin with device in
                                                  SHIP mode. Below MIN, device will not
VBOOT               Boot threshold voltage                                                        300                       1000          mV
                                                  boot up. Above MAX, device will be
                                                  guaranteed to boot up.
                                               Measured at TS1 pin. Below MIN,
                    Boot threshold application
tBOOT_max                                      device will not boot up. Above MAX,                    10                    2000          µs
                    time
                                               device will be guaranteed to boot up.


6.6       Timing Requirements
 2
I C COMPATIBLE INTERFACE                                                                       MIN           TYP          MAX        UNIT
                                                                                                                    REGOUT x
VIL             Input Low Logic Threshold                                                                                                 V
                                                                                                                        0.25
                                                                                          REGOUT x
VIH             Input High Logic Threshold                                                                                                V
                                                                                              0.75
VOL             Output Low Logic Drive                                                                                     0.20           V
tf              SCL, SDA Fall Time                                                                                         0.40
VOH             Output High Logic Drive (Not applicable due to open-drain outputs)             N/A                          N/A           V
tHIGH           SCL Pulse Width High                                                            4.0                                       µs
tLOW            SCL Pulse Width Low                                                             4.7                                       µs
tSU;STA         Setup time for START condition                                                  4.7                                       µs
tHD;STA         START condition hold time after which first clock pulse is generated            4.0                                       µs
tSU;DAT         Data setup time                                                                250                                        ns
tHD;DAT         Data hold time                                                                   0                                        µs
tSU;STO         Setup time for STOP condition                                                   4.0                                       µs
tBUF            Time the bus must be free before new transmission can start                     4.7                                       µs
tVD;DAT         Clock Low to Data Out Valid                                                                                 900           ns
tHD;DAT         Data Out Hold Time After Clock Low                                               0                                        ns
fSCL            Clock Frequency                                                                  0                          100       kHz




                            SCL



                           SDA




                            SCL

                           SDA




                            SCL

                           SDA




                                                          Figure 6-1. I2C Timing

Copyright © 2013–2015, Texas Instruments Incorporated                                                         Electrical Specifications        17
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 18 页

来源 [BQ76940.pdf PDF p.18](../../sources/BQ76940.pdf#page=18)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                                                                                                                          www.ti.com



6.7                                       Typical Characteristics

                                    0.020                                                                                                                                     30
                                                                                                                      VC1 Error
                                    0.018
                                    0.016                                                                             VC2 Error                                               25
                                    0.014                                                                             VC3 Error
                                                                                                                                                                              20




                                                                                                                                                           Gain Error (PPM)
                                    0.012                                                                             VC4 Error
     VCx Error (mV)




                                    0.010                                                                             VC5 Error                                               15
                                    0.008
                                    0.006                                                                                                                                     10
                                    0.004
                                    0.002                                                                                                                                      5

                                    0.000
                                                                                                                                                                               0
                          ±0.002
                          ±0.004                                                                                                                                              ±5
                                2.00 2.30 2.60 2.90 3.20 3.50 3.80 4.10 4.40 4.70 5.00                                                                                              ±40           ±15            10         35      60         85
                                                                       VCx Input (V)                                              C001                                                                          Temperature (ƒC)               C005




 Figure 6-2. bq76930 VCx Error Across Input Range at 25°C with                                                                                  Figure 6-3. Coulomb Counter Gain Error Temperature Drift
                          VIN at 3.6 V                                                                                                                            (from –0.2 V to 0.2 V)

                                           0.0                                                                                                                                0.0

                                          ±0.1                                                                                                                      ±0.2

                                                                                                                                                                    ±0.4
                                          ±0.2
                      Gain Error (%FSR)




                                                                                                                                                                    ±0.6
                                                                                                                                                  Offset (uV)


                                          ±0.3
                                                                                                                                                                    ±0.8
                                          ±0.4
                                                                                                                                                                    ±1.0
                                          ±0.5
                                                                                                                                                                    ±1.2
                                          ±0.6                                                                                                                      ±1.4

                                          ±0.7                                                                                                                      ±1.6
                                                 ±40     ±15           10                              35            60           85                                                ±40           ±15            10         35      60         85
                                                                      Temperature (ƒC)                                            C003                                                                          Temperature (ƒC)               C002




                                                  Figure 6-4. Coulomb Counter Gain Error                                                                                                  Figure 6-5. Coulomb Counter Offset
                                                            (from –0.2 V to 0.2 V)

                                                                                                        0.01


                                                                                                        0.01
                                                                              OV Detection Error (V)




                                                                                                        0.00


                                                                                                       ±0.01


                                                                                                       ±0.01


                                                                                                       ±0.02


                                                                                                       ±0.02
                                                                                                               ±40        ±15             10                      35                        60           85
                                                                                                                                         Temperature (ƒC)                                                C004




                                                                                    Figure 6-6. OV Protection Detection Error (0xFF Setting)




18                                        Electrical Specifications                                                                                                                              Copyright © 2013–2015, Texas Instruments Incorporated
                                                                                                                 Submit Documentation Feedback
                                                                                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 19 页

来源 [BQ76940.pdf PDF p.19](../../sources/BQ76940.pdf#page=19)

```text
                                                                                                                           bq76920, bq76930, bq76940
www.ti.com                                                                                                SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



7 Detailed Description
7.1     Overview
        In the bq769x0 family of analog front-end (AFE) devices, the bq76920 device supports up to 5-series cells,
        the bq76930 device supports up to 10-series cells, and the bq76940 device supports up to 15-series cells.
        Via I2C, a host controller can use the bq769x0 to implement battery pack management functions, such as
        monitoring (cell voltages, pack current, pack temperatures), protection (controlling charge/discharge
        FETs), and balancing. Integrated A/D converters enable a purely digital readout of critical system
        parameters including cell voltages and internal or external temperature, with calibration handled in TI’s
        manufacturing process. For an additional degree of pack reliability, the bq769x0 includes hardware
        protections for voltage (OV, UV) and current (OCD, SCD).
        The bq769x0 provides two low-side FET drivers, charge (CHG) and discharge (DSG), which may be used
        to directly manipulate low-side power NCH FETs, or as signals that control an external circuit that enables
        high-side PCH or NCH FETs. A dedicated ALERT input/output pin serves as an interrupt signal to the host
        microcontroller, quickly informing the microcontroller of an updated status in the AFE. This may include a
        fault event or that a coulomb counter sample is available for reading. An available ALERT pin may also be
        driven externally by a secondary protector to provide a redundant means of disabling the CHG and DSG
        signals and higher system visibility.

7.2     Functional Block Diagram
                                                                                                                               REG
                                                                                                 CAP
                                                                                                                               SRC

                                                                                Bandgap
                 BAT                                  VSTUP/POR
                                                                                 IBIAS



                                       Cell Balance         BOOT
                                       Drivers/FETs                             Internal 3.3-V
                                                                                     LDO
                                                                                                                         External             REG
                                                                                                                      2.5/3.3-V LDO           OUT
                  VCx Inputs




                                                                           14-bit ADC
                                                                           Modulator
                                                            V2I




                                                                                                                                            ALERT



                                                                                      256 kHz
                                                                                                                               FET            CHG
                                                                                                       Digital core         DRIVER and
                               Die temp                                                                                       LOAD
                                                                    CC VREF
                                                                                                                             DETECT           DSG

                                  TS


                                                                   16-bit ACC                                                                 SCL
                                                                   Modulator                                                    I2C
                 TS                                                                                                                           SDA



                                BOOT                                SCD
                                                                    comp




                                                                    OCD                                                           EEPROM
                                To POR
                                                                    comp

                 VSS                              SRP SRN


                                                           Figure 7-1. Functional Block Diagram




Copyright © 2013–2015, Texas Instruments Incorporated                                                                                 Detailed Description   19
                                                                  Submit Documentation Feedback
                                                          Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 20 页

来源 [BQ76940.pdf PDF p.20](../../sources/BQ76940.pdf#page=20)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                          www.ti.com



7.3     Feature Description

7.3.1       Subsystems
        bq769x0 consists of three major subsystems: Measurement, Protection, and Control. These work together
        to ensure that the fundamental battery pack parameters—voltage, current and temperature—are
        accurately captured and easily available to a host controller, while ensuring a baseline or secondary level
        of hardware protection in the event that a host controller is unable or unavailable to manage certain fault
        conditions.

                                                               NOTE
                      The bq769x0 is intended to serve as an analog front-end (AFE) as part of a chipset system
                      solution: A companion microcontroller is required to oversee and control this AFE.

        •    The Measurement subsystem’s core responsibility is to digitize the cell voltages, pack current
             (integrated into a passed charge calculation), external thermistor temperature, and internal die
             temperature. It also performs an automatic calculation of the total battery stack voltage, by simply
             adding up all measured cell voltages.
        •    The Protection subsystem provides a baseline or secondary level of hardware protections to better
             support a battery pack’s FMEA requirements in the event of a loss of host control or simply if a host is
             unable to respond to a certain fault event in time. Integrated protections include pack-level faults such
             as OV, UV, OCD, SCD, detection of an external secondary protector fault, and internal logic
             “watchdog”-style device fault (XREADY). Protection events will trigger toggling of the ALERT pin, as
             well as automatic disabling of the DSG and/or CHG FET driver (depending on the fault). Recovery
             from a fault event must be handled by the host microcontroller.
        •    The Control subsystem implements a suite of useful pack features, including direct low-side NCH FET
             drivers, cell balancing drivers, the ALERT digital output, an external LDO and more.
        The following sections describe each subsystem in greater detail, as well as explaining the various power
        states that are available.

7.3.1.1     Measurement Subsystem Overview
        The monitoring subsystem ensures that all cell voltages, temperatures, and pack current may be easily
        measured by the host. All ADCs are trimmed by TI.
        ADC and CC data are always returned as atomic values if both high and low registers are read in the
        same transaction (using address auto-increment).

7.3.1.1.1 Data Transfer to the Host Controller
        The bq769x0 has a fully digital interface: All information is transferred through I2C, simply by reading
        and/or writing to the appropriate register(s) storing the relevant data. Block reads and writes, buffered by
        an 8-bit CRC code per byte, ensure a fast and robust transmission of data.

7.3.1.1.2 14-Bit ADC
        Each bq769x0 device measures cell voltages and temperatures using a 14-bit ADC. This ADC measures
        all differential cell voltages, thermistors and/or die temperature with a nominal full-scale unsigned range of
        0–6.275 V and LSB of 382 µV.
        To enable the ADC, the [ADC_EN] bit in the SYS_CTRL1 register must be set. This bit is set automatically
        whenever the device enters NORMAL mode. When enabled, the ADC ensures that the integrated OV and
        UV protections are functional.




20      Detailed Description                                                     Copyright © 2013–2015, Texas Instruments Incorporated
                                                 Submit Documentation Feedback
                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 21 页

来源 [BQ76940.pdf PDF p.21](../../sources/BQ76940.pdf#page=21)

```text
                                                                                                      bq76920, bq76930, bq76940
www.ti.com                                                                            SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



        For each contiguous set of five cells (VC1 to VC5, VC6 to VC10), when no cells in that particular set are
        being balanced, each cell is measured over a 50-ms decimation window and a complete update is
        available every 250 ms. In the bq76930 and bq76940, every set of five cells above the primary five cells is
        measured in parallel. The 50-ms decimation greatly assists with removing the aliasing effects present in a
        noisy motor environment.
        When any cells in a contiguous set of 5 cells are being balanced, those affected cells are measured in a
        reduced 12.5-ms decimation period, to allow the cell balancing to function properly without affecting the
        integrated OV and UV protections. Since cell balancing is typically only performed during pack charge or
        idle periods, the shortened decimation periods should not impact accuracy as the system noise during
        these times is greatly reduced. This reduced decimation period is only applied to sets where one of the
        cells is being balanced. The following summarizes this for the bq76920–bq76940 devices:
        • VC1 to VC5 measurements are each taken in a 50-ms decimation period when all bits in CELLBAL1
            register are 0, and a 12.5-ms decimation period when any bits in CELLBAL1 register are 1.
        • VC6 to VC10 measurements are each taken in a 50-ms decimation period when all bits in CELLBAL2
            register are 0, and a 12.5-ms decimation period when any bits in CELLBAL2 register are 1.
        • VC11 to VC15 measurements are each taken in a 50-ms decimation period when all bits in CELLBAL3
            register are 0, and a 12.5-ms decimation period when any bits in CELLBAL3 register are 1.
        • Total update interval is 250 ms.
        Each differential cell input is factory-trimmed for gain and/or offset, such that the resulting reading via I2C
        is always consistent from part-to-part and requires no additional calibration or correction factor application.
        The ADC is required to be enabled in order for the integrated OV and UV protections to be operating.
        The following shows how to convert the 14-bit ADC reading into an analog voltage. Each device is factory
        calibrated, with a GAIN and OFFSET stored into EEPROM.
        The ADC transfer function is a linear equation defined as follows:
        V(cell) = GAIN x ADC(cell) + OFFSET                                                                                         (1)

        GAIN is stored in units of µV/LSB, while OFFSET is stored in mV units.
        Some example cell voltage calculations are provided in the table below. For illustration purposes, the
        example uses a hypothetical GAIN of 380 µV/LSB (ADCGAIN<4:0> = 0x0F) and OFFSET of 30 mV
        (ADCOFFSET<7:0> = 0x1E).

    14-Bit ADC Result           ADC Result in Decimal           GAIN (µV/LSB)             OFFSET (mV)           Cell Voltage (mV)
           0x1800                         6144                       380                         30                   2365
           0x1F10                         7952                       380                         30                   3052



                                                                       NOTE
                        When entering NORMAL mode from SHIP mode, please allow for the following times before
                        reading out initial cell voltage data:
                        bq76920: 250 ms
                        bq76930: 400 ms
                        bq76940: 800 ms




Copyright © 2013–2015, Texas Instruments Incorporated                                                      Detailed Description     21
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 22 页

来源 [BQ76940.pdf PDF p.22](../../sources/BQ76940.pdf#page=22)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                 www.ti.com



7.3.1.1.2.1 Optional Real-time Calibration Using the Host Microcontroller
       The performance of the cell voltage values measured by the 14-bit ADC has a factory-calibrated accuracy,
       as follows:
       • +/– 10 mV TYP, +/– 40 mV MIN and MAX from 3.6 to 4.3 V,
       • +/– 15 mV TYP, +/– 40 mV MIN and MAX from 3.2 to 4.6 V, and
       • +/– 50 mV MIN and MAX from 2.0 to 5.0 V
       While this is suitable for the majority of pack protection and basic monitoring applications the bq769x0
       AFE family is intended to support, certain systems may require a higher accuracy performance.
       To achieve this, use an available ADC channel and general purpose output terminal on the host
       microcontroller paired with the bq769x0. A simple external circuit consisting of two precision resistors and
       a small-signal FET is activated by the host microcontroller to determine the total stack voltage, VSTACK.
       This is then compared against the sum of the individual cell voltages as measured by the internal ADC of
       the bq769x0. The resulting transfer function coefficient, GAIN2, is simply applied to each cell voltage ADC
       value for improved accuracy.




                                                                       Host microcontroller
                   Battery cell stack




                                                                                  A/D input

                                                                                  Gen. purpose output




                             Figure 7-2. External Real-Time Calibration Circuit to Host Microcontroller

       The process is as follows:
       1. Periodically measure VSTACK.
          (a) VSTACK = VAD × (R1 + R2) / R1
       2. Read out all VCELL ADC readings from the bq769x0 and apply the standard GAIN and OFFSET values
          stored in the bq769x0.
          (a) V(1) = GAIN x ADC1 + OFFSET, V(2) = GAIN x ADC2 + OFFSET, and so on
       3. Sum up all VCELL values, VSUM.
          (a) VSUM = V(1) + V(2) + V(3) …
       4. Calculate GAIN2.
          (a) GAIN2 = VSTACK / VSUM
       As a general recommendation, a new GAIN2 function should be generated when the cell voltages increase
       or decrease by more than 100 mV. With GAIN2, each cell voltage calculation becomes:
       V(cell) = GAIN2 × (GAIN x ADC(cell) + OFFSET)                                                                                     (2)

       For systems that do not require this additional in-use calibration function, GAIN2 is simply “1”.

22     Detailed Description                                                             Copyright © 2013–2015, Texas Instruments Incorporated
                                                      Submit Documentation Feedback
                                              Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 23 页

来源 [BQ76940.pdf PDF p.23](../../sources/BQ76940.pdf#page=23)

```text
                                                                                                       bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



7.3.1.1.3 16-Bit CC
        A 16-bit integrating ADC, commonly referred to as the coulomb counter (CC), provides measurements of
        accumulated charge across the current sense resistor. The integration period for this reading is 250 ms.
        The CC may be operated in one of two modes: ALWAYS ON and 1-SHOT.
        • In ALWAYS ON mode, the CC runs at 100%, gathering a fresh reading every 250 ms. The conclusion
           of each reading sets the CC_READY bit, which toggles the ALERT pin high to inform the
           microcontroller that a new reading is available. To enable Always On mode, set [CC_EN] = 1.
        • In 1-SHOT mode, the CC performs a single 250-ms reading, and similarly sets the CC_READY bit
           when completed. This mode is intended for non-gauging usages, where the host simply desires to
           check the pack current.
           To enable a 1-SHOT reading, ensure [CC_EN] = 0 and set [CC_ONESHOT] = 1.
        The full scale range of the CC is ± 270 mV, with a max recommended input range of ± 200 mV, thus
        yielding an LSB of approximately 8.44 µV.
        The following equation shows how to convert the 16-bit CC reading into an analog voltage if no board-
        level calibration is performed:
        CC Reading (in µV) = [16-bit 2’s Complement Value] × (8.44 µV/LSB)                                                              (3)



                           16-Bit CC Result                ADC Result in Decimal                CC Reading (in µV)
                                 0x0001                              1                                 8.44
                                 0x2710                            10000                             84,400
                                0x7D00                             32000                             270,080
                                 0x8300                            –32000                           –270,080
                                0xC350                             –15536                          –131,123.84
                                0xFFFF                               –1                               –8.44


7.3.1.1.4 External Thermistor
        One (bq76920), two (bq76930), or three (bq76940) 10 kΩ NTC 103AT thermistors may be measured by
        the device. These are measured by applying a factory-trimmed internal 10k pull-up resistance to an
        internal regulator value of nominally 3.3 V, the result of which can be read out from the TSx (TS1, TS2,
        TS3) registers.
        To select thermistor measurement mode, set [TEMP_SEL] = 1.
        Thermistor TS1 is connected between TS1 and VSS; TS2 is connected between TS2 and VC5x (bq76930
        and bq76940 only); and TS3 is connected between TS3 and VC10x (bq76940 only). These thermistors
        may be placed in various areas in the battery pack to measure such things as localized cell temperature,
        FET heating, etc.
        The thermistor impedance may be calculated using the 14-bit ADC reading in the TS1/TS2/TS3 registers
        and 10k internal pull-up resistance as follows:
        The following equations show how to use the 14-bit ADC readings in TS1, TS2, and TS3 to determine the
        resistance of the external 103AT thermistor:
        VTSX = (ADC in Decimal) x 382 µV/LSB                                                                                            (4)
        RTS = (10,000 × VTSX) ÷ (3.3 – VTSX)                                                                                            (5)

        To convert the thermistor resistance into temperature, please refer to the thermistor component
        manufacturer’s datasheet.




Copyright © 2013–2015, Texas Instruments Incorporated                                                            Detailed Description   23
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 24 页

来源 [BQ76940.pdf PDF p.24](../../sources/BQ76940.pdf#page=24)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                         www.ti.com



7.3.1.1.5 Die Temperature Monitor

                                                              NOTE
                     When switching between external and internal temperature monitoring, a 2-s latency may be
                     incurred due to the natural scheduler update interval.


       A die temperature block generates a voltage that is proportional to the die temperature, and provides a
       way of reducing component count if pack thermistors are not used or ensuring that the die power
       dissipation requirements are observed. The die is measured using the same on-board 14-bit ADC as the
       cell voltages.
       To select internal die temperature measurement mode, set [TEMP_SEL] = 0.
       For bq76930 and bq76940, multiple die temperature measurements are available. These are stored in
       TS2 and TS3.
       To convert a DIETEMP reading into temperature, refer to the following equation box. If more accurate
       temperature readings are needed from DIETEMP, the DIETEMP at room temperature value should be
       stored during production calibration.
       The following equation shows how to use the 14-bit ADC readings in TS1, TS2, and TS3 when
       [TEMPSEL] = 0 to determine the internal die temperature:

       V25 = 1.200 V (nominal)                                                                                                   (6)
       VTSX = (ADC in Decimal) x 382 µV/LSB                                                                                      (7)
       TEMPDIE = 25° – ((VTSX – V25) ÷ 0.0042)                                                                                   (8)

7.3.1.1.6 16-Bit Pack Voltage
       Once converted to digital form, each cell voltage is added up and the summation result stored in the BAT
       registers. This 16-bit value has a nominal LSB of 1.532 mV.
       The following shows how to convert the 16-bit pack voltage ADC reading into an analog voltage. This
       value also uses the GAIN and OFFSET stored into EEPROM.
       The ADC transfer function is a linear equation defined as follows:
       V(BAT) = 4 x GAIN x ADC(cell) + (#Cells x OFFSET)                                                                         (9)

       GAIN is stored in units of µV/LSB, while OFFSET is stored in mV units.

7.3.1.1.7 System Scheduler
       A master scheduler oversees the monitoring intervals, creating a full update every 250 ms. Temperature
       measurements are taken every 2 seconds. Pack voltage is calculated every 250 ms.

7.3.1.2   Protection Subsystem

7.3.1.2.1 Integrated Hardware Protections
       Integrated hardware protections are provided as an extra degree of safety and are meant to supplement
       the standard protection feature set that would be incorporated into the host controller firmware. They
       should not be used as the sole means of protecting a battery pack, but are useful for FMEA purposes; for
       example, in the event that a host microcontroller is unable to react to any of the below protection
       situations. All hardware protection thresholds and delays should be loaded into the AFE by the host
       microcontroller during system startup. The AFE will also default to pre-defined threshold and delay
       settings, in case the host microcontroller is unable to or does not wish to program the protection settings.




24     Detailed Description                                                     Copyright © 2013–2015, Texas Instruments Incorporated
                                                Submit Documentation Feedback
                                        Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 25 页

来源 [BQ76940.pdf PDF p.25](../../sources/BQ76940.pdf#page=25)

```text
                                                                                                    bq76920, bq76930, bq76940
www.ti.com                                                                            SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



        Overcurrent in Discharge (OCD) and Short Circuit in Discharge (SCD) are implemented using sampled
        analog comparators that run at 32 kHz, and that continuously monitor the voltage across (SRP–SRN)
        while the device is in NORMAL mode. Upon detection of a voltage that exceeds the programmed OCD or
        SCD threshold, a counter begins to count up to a programmed delay setting. If the counter reaches its
        target value, the SYS_STAT register is updated to indicate the fault condition, the FET state(s) are
        updated as shown in
        Table 7-1, and the ALERT pin is driven high to interrupt the host.
        The protection fault threshold and delay settings for OCD and SCD protections are configured via the
        PROTECT1 and PROTECT2 registers. See Section 7.5 for details about supported values.
        Overvoltage (OV) and Undervoltage (UV) protections are handled digitally, by comparing the cell voltage
        readings against the 8-bit programmed thresholds in the OV and UV registers.
        The OV threshold is stored in the OV_TRIP register and is a direct mapping of 8 bits of the 14-bit ADC
        reading, with the upper 2 MSB preset to “10” and lower 4 LSB preset to “1000”. In other words, the
        corresponding OV trip level is mapped to “10-XXXX-XXXX–1000”. The programmable range of OV
        thresholds is approximately 3.15 to 4.7 V, but this is subject to variation due to
        the (GAIN, OFFSET) linear equation used to map the ADC values.
        The UV threshold is stored in the UV_TRIP register and is a direct mapping of 8 bits of the 14-bit ADC
        reading, with the upper 2 MSB preset to “01” and lower 4 LSB preset to “0000”. In other words, the
        corresponding OV trip level is mapped to “01-XXXX-XXXX–0000”. The programmable range of UV
        thresholds is approximately 1.58 to 3.1 V, but this is subject to variation due to the (GAIN, OFFSET) linear
        equation used to map the ADC values.

                          Protection                    Upper 2 MSB         Middle 8 Bits           Lower 4 LSB
                              OV                            10         Set in OV_TRIP Register          1000
                              UV                            01         Set in UV_TRIP Register          0000



                                                                      NOTE
                        To support flexible cell configurations within bq76920, bq76930, and bq76940, UV is ignored
                        on any cells that have a reading under UVMINQUAL. This allows cell pins to be shorted in
                        implementations where not all cells are needed (for example, 6-series cells using the
                        bq76930).


        Default protection thresholds and delays are shown in the register description at the end of this datasheet.
        These are loaded into the digital register (RAM) of the device when the device enters NORMAL mode.
        These RAM values may then be overwritten by the host controller to any other values, which they will
        retain until a POR event. It is recommended that the host controller reload these values during its standard
        power-up and/or re-initialization sequence.
        To calculate the correct OV_TRIP and UV_TRIP register values for a device, use the following procedure:
        1. Determine desired OV.
        2. Read out [ADCGAIN] and [ADCOFFSET] from their corresponding registers. Note that ADCGAIN is
           stored in units of µV/LSB, while ADCOFFSET is stored in mV.
        3. Calculate the full 14-bit ADC value needed to meet the desired OV and UV trip thresholds as follows:
           (a) OV_TRIP_FULL = (OV – ADCOFFSET) ÷ ADCGAIN
           (b) UV_TRIP_FULL = (UV – ADCOFFSET) ÷ ADCGAIN
        4. Remove the upper 2 MSB and lower 4 LSB from the full 14-bit value, retaining only the remaining
           middle 8 bits. This can be done by shifting the OV_TRIP_FULL and UV_TRIP_FULL binary values 4
           bits to the right and removing the upper 2 MSB.
        5. Write OV_TRIP and UV_TRIP to their corresponding registers.


Copyright © 2013–2015, Texas Instruments Incorporated                                                      Detailed Description   25
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 26 页

来源 [BQ76940.pdf PDF p.26](../../sources/BQ76940.pdf#page=26)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                                          www.ti.com



       Both OV and UV protections require the ADC to be enabled. Ensure that the [ADC_EN] bit is set to 1 if
       OV and UV protections are needed.

7.3.1.2.2 Reduced Test Time
       A special debug and test configuration bit is provided in the SYS_CTRL2 register, called [DELAY_DIS].
       Setting [DELAY_DIS] bypasses the OV/UV protection fault timers and allows a fault condition to be
       registered within 200 ms after application of such a fault condition.

7.3.1.3   Control Subsystem

7.3.1.3.1 FET Driving (CHG AND DSG)
       Each bq769x0 device provides two low-side FET drivers, CHG and DSG, which control NCH power FETs
       or may be used as a signal to enable various other circuits such as a high-side NCH charge pump circuit.
       Both DSG and CHG drivers have a fast pull-up to nominally 12 V when enabled. DSG uses a fast pull-
       down to VSS when disabled, while CHG utilizes a high impedance (nominally 1 MΩ) pull-down path when
       disabled.
       An additional internal clamp circuit ensures that the CHG pin does not exceed a maximum of 20 V.

                                              DSG                                     CHG

                                                                                      Q3 is a low-cost PCH FET and is used to keep CHG away
                                                                                      from any voltages below VSS. When CHG is not being
                                                                                      pulled high, PACK± being pulled below VSS will not be
                                                                                      seen by CHG as Q2 does not turn on. Q3 also allows R2 to
                                                                                      keep Q1 OFF, since all voltages below this FET can
                                                                              Q3      "follow" PACK± as it goes below VSS.



                                                                                               R1 drops the voltage when PACK± is pulled high and
                                                          This diode allows CHG           R1 limits the current going into the CHG pin. Since CHG
                                                         to pull the Q1 gate high.       (1M) clamps at ~ 18 V, R1 will limit current to approximately
                                                                                               (V(PACK±) - 18) / R1.




                                                                                                   This zener clamp may
                                                                                           R2      be needed to prevent
                                         R2                                                        Q1 from turning on
                                                                                          (1M)
                                                         Q2         Q1                             too quickly (optional).
              BAT±
                                                                                                                                  PACK t
                     Rsns                                                            R2 clamps Q1 when
                                                                                     CHG is turned off.




                                                Figure 7-3. CHG and DSG FET Circuit

       The power path for the CHG and DSG pull-up circuit originates from the REGSRC pin, instead of BAT.
       To enable the CHG fet, set the [CHG_ON] register bit to 1; to disable, set [CHG_ON] = 0. The discharge
       FET may be similarly controlled via the [DSG_ON] register bit.
       Certain fault conditions or power state transitions will clear the state of the CHG/DSG FET controls.
       Table 7-1 shows what action, if any, to take to [CHG_ON] and [DSG_ON] in response to various system
       events:


                              Table 7-1. CHG, DSG Response Under Various System Events
                               EVENT                                 [CHG_ON]                                     [DSG_ON]
                              OV Fault                                   Set to 0                                       —
                              UV Fault                                      —                                       Set to 0
                              OCD Fault                                     —                                       Set to 0


26     Detailed Description                                                                          Copyright © 2013–2015, Texas Instruments Incorporated
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 27 页

来源 [BQ76940.pdf PDF p.27](../../sources/BQ76940.pdf#page=27)

```text
                                                                                                   bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



                      Table 7-1. CHG, DSG Response Under Various System Events (continued)
                                 EVENT                           [CHG_ON]                       [DSG_ON]
                               SCD Fault                             —                            Set to 0
                            ALERT Override                         Set to 0                       Set to 0
                       DEVICE_XREADY is set                        Set to 0                       Set to 0
                   Enter SHIP mode from NORMAL                     Set to 0                       Set to 0



                                                                       NOTE
                        All protection recovery must be initiated by the host microcontroller. In order to resume FET
                        operation after a fault condition has occurred, the host microcontroller must first clear the
                        corresponding status bit in the SYS_STAT register, which will clear the ALERT pin, and then
                        manually re-enable the CHG and/or DSG bit. Certain faults, such as OV or UV, may
                        immediately re-toggle if such a condition still persists. Refer to Table 7-3 for details on
                        clearing status bits.


        There are no conditions under which the bq769x0 automatically sets either [CHG_ON] or [DSG_ON] to 1.

7.3.1.3.2 Load Detection
        A load detection circuit is present on the CHG pin and activated whenever the CHG FET is disabled
        ([CHG_ON] = 0). This circuit detects if the CHG pin is externally pulled high when the high impedance
        (approximately 1 MΩ) pull-down path should actually be holding the CHG pin to VSS, and is useful for
        determining if the PACK– pin (outside of the AFE) is being held at a high voltage—for example, if the load
        is present while the power FETs are off. The state of the load detection circuit is read from the
        [LOAD_PRESENT] bit of the SYS_CTRL1 register.
        After an OCD or SCD fault has occurred, the DSG FET will be disabled ([DSG_ON] cleared), and the
        CHG FET must similarly be explicitly disabled to activate the load detection circuit. The host
        microcontroller may periodically poll the [LOAD_PRESENT] bit to determine the state of the PACK– pin
        and determine when the load is removed ([LOAD_PRESENT] = 0).

7.3.1.3.3 Cell Balancing
        Both internal and external passive cell balancing options are fully supported by the bq76920, while
        external cell balancing is recommended for bq76930 and bq76940. It is left to the host controller to
        determine the exact balancing algorithm to be used in any given system. Each bq769x0 device provides
        the cell voltages and balancing drivers to enable this. If using the internal cell balance drivers, up to 50 mA
        may be balanced per cell. If using external cell balancing, much higher balancing currents may be
        employed.
        To activate a particular cell balancing channel, simply set the corresponding bit for that cell in the
        CELLBAL1, CELLBAL2, or CELLBAL3 register. For example, VC1–VC0 is enabled by setting [CB1], while
        VC12–VC11 is set via [CB12].
        Multiple cells may be simultaneously balanced. It is left to the user’s discretion to determine the ideal
        number of cells to concurrently balance. Adjacent cells should not be balanced simultaneously. This may
        cause cell pins to exceed their absolute maximum conditions and is also not recommended for external
        balancing implementations. Additionally, if internal balancing is used, care should be taken to avoid
        exceeding package power dissipation ratings.




Copyright © 2013–2015, Texas Instruments Incorporated                                                        Detailed Description   27
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 28 页

来源 [BQ76940.pdf PDF p.28](../../sources/BQ76940.pdf#page=28)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                         www.ti.com




                                                              NOTE
                     The host controller must ensure that no two adjacent cells are balanced simultaneously
                     within each set of the following:
                     • VC1–VC5
                     • VC6–VC10
                     • VC11–VC15


       The total duty cycle devoted to balancing is approximately 70% per 250 ms. This is because a portion of
       the 250 ms is allotted for normal cell voltage measurements via the ADC.
       If [ADC_EN] =1, OV and UV protections are not affected by cell balancing, since the cell balancing is
       temporarily suspended for a small slice of time every 250 ms during which the cell voltage readings are
       taken. This ensures that the OV and UV protections do not accidentally trigger, or miss an actual OV/UV
       condition on the cells while balancing is enabled.

                                                              NOTE
                     All cell balancing control bits in CELLBAL1, CELLBAL2, and CELLBAL3 are automatically
                     cleared under the following events, and must be explicitly re-written by the host
                     microcontroller following clearing of the event:
                     • DEVICE_XREADY is set
                     • Enters NORMAL mode from SHIP mode


7.3.1.3.4 Alert
       The ALERT pin serves as an active high digital interrupt signal that can be connected to a GPIO port of
       the host microcontroller. This signal is an OR of all bits in the SYS_STAT register.
       In order to clear the ALERT signal, the source bit in the SYS_STAT register must first be cleared by
       writing a “1” to that bit. This will cause an automatic clear of the ALERT pin once all bits are cleared.
       The ALERT pin may also be driven by an external source; for example, the pack may include a secondary
       overvoltage protector IC. When the ALERT pin is forced high externally while low, the device will
       recognize this as an OVRD_ALERT fault and set the [OVRD_ALERT] bit. This triggers automatic disabling
       of both CHG and DSG FET drivers. The device cannot recognize the ALERT signal input high when it is
       already forcing the ALERT signal high from another condition.
       The ALERT pin has no internal debounce support so care should be taken to protect the pin from noise or
       other parasitic transients.

                                                              NOTE
                     It is highly recommended to place an external 500 kΩ–1 MΩ pull-down resistor from ALERT
                     to VSS as close to the IC as possible. Additional recommendations are:
                          a) To keep all traces between the IC and components connected to the ALERT pin very
                          short.
                          b) To include a guard ring around the components connected to the ALERT pin and the
                          pin itself.


7.3.1.3.5 Output LDO
       An adjustable output voltage regulator LDO is provided as a simple way to provide power to additional
       components in the battery pack, such as the host microcontroller or LEDs. The LDO is configured in
       EEPROM by TI during the production test process, and can support 2.5-V or 3.3-V options.




28     Detailed Description                                                     Copyright © 2013–2015, Texas Instruments Incorporated
                                                Submit Documentation Feedback
                                        Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 29 页

来源 [BQ76940.pdf PDF p.29](../../sources/BQ76940.pdf#page=29)

```text
                                                                                                        bq76920, bq76930, bq76940
www.ti.com                                                                             SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



        A cascode small-signal FET must be added in the external path between BAT and REGSRC with the
        bq76930 and bq76940. This helps drop most of the power dissipation outside of the package and cuts
        down on package power dissipation.

7.3.1.4     Communications Subsystem
        The AFE implements a standard 100-kHz I2C interface and acts as a slave device. The I2C device
        address is 7-bits and is factory programmed. Consult the Device Comparison Table (Section 4) of this
        datasheet for more information.
        A write transaction is shown in Figure 7-4. Block writes are allowed by sending additional data bytes
        before the Stop. The I2C block will auto-increment the register address after each data byte.
        When enabled, the CRC is calculated as follows:
        • In a single-byte write transaction, the CRC is calculated over the slave address, register address, and
          data.
        • In a block write transaction, the CRC for the first data byte is calculated over the slave address,
          register address, and data. The CRC for subsequent data bytes is calculated over the data byte only.
        The CRC polynomial is x8 + x2 + x + 1, and the initial value is 0.
        When the slave detects a bad CRC, the I2C slave will NACK the CRC, which causes the I2C slave to go to
        an idle state.

                  SCL


                 SDA             A6 A5 ... A0 R/W ACK    R7 R6 ... R0 ACK    D7 D6 ... D0 ACK    C7 C6 ... C0 ACK

                         Start       Slave Address            Register              Data                 CRC             Stop
                                                              Address                                  (optional)

                                                           Figure 7-4. I2C Write

        Figure 7-5 shows a read transaction using a Repeated Start.

                SCL


                SDA              A6 A5 ... A0 R/W ACK    R7 R6 ... R0 ACK       A6 A5 ... A0 R/W ACK

                        Start        Slave Address            Register               Slave Address
                                                              Address
                                                                         Repeated
                                                                           Start




                                                                            D7 D6 ... D0 ACK     C7 C6 ... C0 NACK
                                                                                   Slave                Slave          Stop
                                                                                Drives Data          Drives CRC
                                                                                                      (optional) Master
                                                                                                                 Drives NACK

                                               Figure 7-5. I2C Read with Repeated Start

        Figure 7-6 shows a read transaction where a Repeated Start is not used, for example if not available in
        hardware. For a block read, the master ACK’s each data byte except the last and continues to clock the
        interface. The I2C block will auto-increment the register address after each data byte.
        When enabled, the CRC for a read transaction is calculated as follows:
        • In a single-byte read transaction, the CRC is calculated after the second start and uses the slave
          address and data byte.




Copyright © 2013–2015, Texas Instruments Incorporated                                                               Detailed Description   29
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 30 页

来源 [BQ76940.pdf PDF p.30](../../sources/BQ76940.pdf#page=30)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                      www.ti.com



        •    In a block read transaction, the CRC for the first data byte is calculated after the second start and uses
             the slave address and data byte. The CRC for subsequent data bytes is calculated over the data byte
             only.
        The CRC polynomial is x8 + x2 + x + 1, and the initial value is 0.
        When the master detects a bad CRC, the I2C master will NACK the CRC, which causes the I2C slave to
        go to an idle state.

               SCL


               SDA           A6 A5 ... A0 R/W ACK     R7 R6 ... R0 ACK                A6 A5 ... A0 R/W ACK

                     Start       Slave Address             Register      Stop Start       Slave Address
                                                           Address




                                                                           D7 D6 ... D0 ACK         C7 C6 ... C0 NACK
                                                                                  Slave                  Slave          Stop
                                                                               Drives Data            Drives CRC
                                                                                                       (optional) Master
                                                                                                                  Drives NACK

                                         Figure 7-6. I2C Read Without Repeated Start

7.4     Device Functional Modes
        Each bq769x0 device supports the following modes of operation.


                                                 Table 7-2. Supported Power Modes
                     Mode                                              Description
                                 Fully operational state. Both ADC and CC may be on, or disabled by host microcontroller.
                                 OV and UV protection enabled if ADC is on. OCD and SCD enabled. ADC and CC may be
                   NORMAL
                                 disabled to reduce power consumption, and CC may be operated in a “1-SHOT” mode for
                                 flexible power savings.
                                 Lowest possible power state, intended for pack assembly and/or long term pack storage.
                     SHIP        Must see a BOOT signal (> 1 VBOOT) on TS1 pin to boot from SHIP → NORMAL. Note
                                 that the device always enters SHIP mode upon POR.


7.4.1       NORMAL Mode
        NORMAL mode represents the fully operational mode where all blocks are enabled and the device sees
        its highest current consumption. In this mode, certain blocks/functions may be disabled to save
        power—these include the ADC and CC. OV and UV are running continuously as long as the ADC is
        enabled. The OCD and SCD comparators may not be disabled in this mode.
        Transitioning from NORMAL to SHIP mode is also initiated by the host, and requires consecutive writes to
        two bits in the SYS_CTRL1 register.

7.4.2       SHIP Mode
        SHIP mode is the basic and lowest power mode that bq769x0 supports. SHIP mode is automatically
        entered during initial pack assembly and after every POR event. When the device is in NORMAL mode, it
        may enter SHIP by the host controller via a specific sequence of I2C commands.
        In SHIP mode, only a minimum of blocks are turned on, including the VSTUP power supply and primal
        boot detector. Waking from SHIP mode to NORMAL mode requires pulling the TS1 pin greater than
        VBOOT, which triggers the device boot-up sequence.



30      Detailed Description                                                                 Copyright © 2013–2015, Texas Instruments Incorporated
                                                     Submit Documentation Feedback
                                             Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 31 页

来源 [BQ76940.pdf PDF p.31](../../sources/BQ76940.pdf#page=31)

```text
                                                                                                   bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



        To enter SHIP mode from NORMAL mode, the [SHUT_A] and [SHUT_B] bits in the SYS_CTRL1 register
        must be written with specific patterns across two consecutive writes:
        • Write #1: [SHUT_A] = 0, [SHUT_B] = 1
        • Write #2: [SHUT_A] = 1, [SHUT_B] = 0
        Note that [SHUT_A] and [SHUT_B] should each be in a 0 state prior to executing the shutdown command
        above. If this specific sequence is entered into the device, the device transitions into SHIP mode. If any
        other sequence is written to the [SHUT_A] and [SHUT_B] bits or if either of the two patterns is not
        correctly entered, the device will not enter SHIP mode.

                                                                    CAUTION
                          DO NOT OPERATE THE DEVICE BELOW POR. When designing with the
                          bq76940, the intermediate voltages (BAT–VC10x), (VC10x–VC5x), and
                          (VC5x–VSS) must each never fall below VSHUT. When this occurs, a full device
                          reset must be initiated by powering down all three intermediate voltages
                          (BAT–VC10x), (VC10x–VC5x), and (VC5x–VSS) below VSHUT and rebooting by
                          applying the appropriate VBOOT signal to the TS1 pin. When designing with
                          the bq76930, the intermediate voltages (BAT–VC5x) and (VC5x–VSS) must
                          each never fall below VSHUT. If this occurs, a full device reset must be initiated
                          by powering down both intermediate voltages (BAT–VC5x) and (VC5x–VSS)
                          below VSHUT and rebooting by applying the appropriate VBOOT signal to the
                          TS1 pin.


        The device will also enter SHIP mode during a POR event; however, this is not a recommended method
        of SHIP mode entry. If any of the supply-side voltages below fall below VSHUT and then back up above
        VPORA, the device defaults into the SHIP mode state. This is similar to an initial pack assembly condition.
        In order to exit SHIP mode into NORMAL mode, the device must follow the standard boot sequence by
        applying a voltage greater than the VBOOT threshold on the TS1 pin.




Copyright © 2013–2015, Texas Instruments Incorporated                                                     Detailed Description   31
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 32 页

来源 [BQ76940.pdf PDF p.32](../../sources/BQ76940.pdf#page=32)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                           www.ti.com



7.5         Register Maps
       Name           Addr            D7              D6          D5              D4             D3               D2               D1        D0
  SYS_STAT            0x00         CC_READY       RSVD       DEVICE_         OVRD_               UV               OV              SCD       OCD
                                                             XREADY          ALERT
  CELLBAL1            0x01           RSVD         RSVD        RSVD                                              CB<5:1>
 CELLBAL2 (1)         0x02           RSVD         RSVD        RSVD                                          CB<10:6>
 CELLBAL3 (2)         0x03           RSVD         RSVD        RSVD                                          CB<15:11>
 SYS_CTRL1            0x04          LOAD_         RSVD        RSVD           ADC_EN        TEMP_SEL              RSVD            SHUT_A   SHUT_B
                                   PRESENT
 SYS_CTRL2            0x05         DELAY_DIS     CC_EN        CC_                               RSVD                             DSG_ON   CHG_ON
                                                            ONESHOT
 PROTECT1             0x06           RSNS         RSVD        RSVD                 SCD_DELAY                                SCD_THRESH
 PROTECT2             0x07           RSVD                  OCD_DELAY                                               OCD_THRESH
 PROTECT3             0x08                 UV_DELAY                    OV_DELAY                                           RSVD
      OV_TRIP         0x09                                                         OV_THRESH
      UV_TRIP         0x0A                                                         UV_THRESH
      CC_CFG          0x0B           RSVD         RSVD                                     Must be programmed to 0x19
      VC1_HI          0x0C           RSVD         RSVD                                                 <13:8>
      VC1_LO          0x0D                                                             <7:0>
      VC2_HI          0x0E           RSVD         RSVD                                                 <13:8>
      VC2_LO          0x0F                                                             <7:0>
      VC3_HI          0x10           RSVD         RSVD                                                 <13:8>
      VC3_LO          0x11                                                             <7:0>
      VC4_HI          0x12           RSVD         RSVD                                                 <13:8>
      VC4_LO          0x13                                                             <7:0>
      VC5_HI          0x14           RSVD         RSVD                                                 <13:8>
      VC5_LO          0x15                                                             <7:0>
     VC6_HI (1)       0x16           RSVD         RSVD                                                 <13:8>
              (1)
  VC6_LO              0x17                                                             <7:0>
     VC7_HI (1)       0x18           RSVD         RSVD                                                 <13:8>
  VC7_LO (1)          0x19                                                             <7:0>
     VC8_HI (1)       0x1A           RSVD         RSVD                                                 <13:8>
  VC8_LO (1)          0x1B                                                             <7:0>
     VC9_HI (1)       0x1C           RSVD         RSVD                                                 <13:8>
  VC9_LO (1)          0x1D                                                             <7:0>
  VC10_HI (1)         0x1E           RSVD         RSVD                                                 <13:8>
 VC10_LO (1)          0x1F                                                             <7:0>
  VC11_HI (2)         0x20           RSVD         RSVD                                                 <13:8>
 VC11_LO (2)          0x21                                                             <7:0>
  VC12_HI (2)         0x22           RSVD         RSVD                                                 <13:8>
 VC12_LO (2)          0x23                                                             <7:0>
  VC13_HI (2)         0x24           RSVD         RSVD                                                 <13:8>
 VC13_LO (2)          0x25                                                             <7:0>
  VC14_HI (2)         0x26           RSVD         RSVD                                                 <13:8>
 VC14_LO (2)          0x27                                                             <7:0>
  VC15_HI (2)         0x28           RSVD         RSVD                                                 <13:8>
 VC15_LO (2)          0x29                                                             <7:0>
      BAT_HI          0x2A                                                             <15:8>
      BAT_LO          0x2B                                                             <7:0>
       TS1_HI         0x2C           RSVD         RSVD                                                 <13:8>
      TS1_LO          0x2D                                                             <7:0>
      TS2_HI (1)      0x2E           RSVD         RSVD                                                 <13:8>
  TS2_LO (1)          0x2F                                                             <7:0>

(1)     These registers are only valid for bq76930 and bq76940.
(2)     These registers are only valid for bq76940.
32          Detailed Description                                                                 Copyright © 2013–2015, Texas Instruments Incorporated
                                                         Submit Documentation Feedback
                                                 Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 33 页

来源 [BQ76940.pdf PDF p.33](../../sources/BQ76940.pdf#page=33)

```text
                                                                                                             bq76920, bq76930, bq76940
www.ti.com                                                                                   SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015


    Name            Addr            D7             D6             D5           D4              D3             D2            D1             D0
  TS3_HI (2)        0x30          RSVD           RSVD                                               <13:8>
  TS3_LO (2)        0x31                                                            <7:0>
    CC_HI           0x32                                                            <15:8>
    CC_LO           0x33                                                            <7:0>
  ADCGAIN1          0x50                                  RSVD                                  ADCGAIN<4:3>                     RSVD
 ADCOFFSET          0x51                                                       ADCOFFSET<7:0>
  ADCGAIN2          0x59                      ADCGAIN<2:0>                                                   RSVD


7.5.1       Register Details

                                                         Table 7-3. SYS_STAT (0x00)
      BIT               7                6                 5             4             3                 2              1                0
    NAME          CC_READY            RSVD              DEVICE_        OVRD_           UV               OV            SCD               OCD
                                                        XREADY         ALERT
    RESET               0                0                 0             0             0                 0              0                0
   ACCESS              RW                RW               RW            RW            RW                RW             RW               RW



                                                                         NOTE
                        Bits in SYS_STAT may be cleared by writing a "1" to the corresponding bit.
                        Writing a "0" does not change the state of the corresponding bit.


         CC_READY (Bit 7): Indicates that a fresh coulomb counter reading is available. Note that if this bit is not
         cleared between two adjacent CC readings becoming available, the bit remains latched to 1. This bit may
         only be cleared (and not set) by the host.
                    0 = Fresh CC reading not yet available or bit is cleared by host microcontroller.
                    1 = Fresh CC reading is available. Remains latched high until cleared by host.

         RSVD (Bit 6): Reserved. Do not use.

         DEVICE_XREADY (Bit 5): Internal chip fault indicator. When this bit is set to 1, it should be cleared by
         the host. May be set due to excessive system transients. This bit may only be cleared (and not set) by
         the host.
                    0 = Device is OK.
                    1 = Internal chip fault detected, recommend that host microcontroller clear this bit after waiting a
                        few seconds. Remains latched high until cleared by the host.

         OVRD_ALERT (Bit 4): External pull-up on the ALERT pin indicator. Only active when ALERT pin is not
         already being driven high by the AFE itself.
                    0 = No external override detected
                    1 = External override detected. Remains latched high until cleared by the host.

         UV (Bit 3): Undervoltage fault event indicator.
                    0 = No UV fault is detected.
                    1 = UV fault is detected. Remains latched high until cleared by the host.

         OV (Bit 2): Overvoltage fault event indicator.
                    0 = No OV fault is detected.
                    1 = OV fault is detected. Remains latched high until cleared by the host.


Copyright © 2013–2015, Texas Instruments Incorporated                                                               Detailed Description        33
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 34 页

来源 [BQ76940.pdf PDF p.34](../../sources/BQ76940.pdf#page=34)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                         www.ti.com



        SCD (Bit 1): Short circuit in discharge fault event indicator.
                  0 = No SCD fault is detected.
                  1 = SCD fault is detected. Remains latched high until cleared by the host.

        OCD (Bit 0): Over current in discharge fault event indicator.
                  0 = No OCD fault is detected.
                  1 = OCD fault is detected. Remains latched high until cleared by the host.

                           Table 7-4. CELLBAL1 (0x01) for bq76920, bq76930, and bq76940
      BIT             7           6               5         4             3               2                1                0
     NAME            —            —               —        CB5           CB4            CB3              CB2              CB1
     RESET            0           0               0         0             0               0                0                0
     ACCESS           R           R               R        RW            RW              RW              RW               RW


        CBx (Bits 4–0):
                  0 = Cell balancing on Cell “x” is disabled.
                  1 = Cell balancing on Cell “x” is enabled.

                               Table 7-5. CELLBAL2 (0x02) for bq76930 and bq76940
      BIT             7           6               5         4             3               2                1                0
     NAME            —            —               —        CB10          CB9            CB8              CB7              CB6
     RESET            0           0               0         0             0               0                0                0
     ACCESS           R           R               R        RW            RW              RW              RW               RW

        CBx (Bits 4–0):
                  0 = Cell balancing on Cell “x” is disabled.
                  1 = Cell balancing on Cell “x” is enabled.

                                      Table 7-6. CELLBAL3 (0x03) for bq76940
      BIT             7           6               5         4             3               2                1                0
     NAME            —            —               —        CB15          CB14           CB13            CB12             CB11
     RESET            0           0               0         0             0               0                0                0
     ACCESS           R           R               R        RW            RW              RW              RW               RW

        CBx (Bits 4–0):
                  0 = Cell balancing on Cell “x” is disabled.
                  1 = Cell balancing on Cell “x” is enabled.

                                             Table 7-7. SYS_CTRL1 (0x04)
      BIT             7           6               5         4             3               2                1                0
     NAME         LOAD_           —               —      ADC_EN       TEMP_SEL         RSVD            SHUT_A          SHUT_B
                 PRESENT
     RESET            0           0               0         0             0               0                0                0
     ACCESS           R           R               R        RW            RW              RW              RW               RW




34      Detailed Description                                                    Copyright © 2013–2015, Texas Instruments Incorporated
                                               Submit Documentation Feedback
                                       Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 35 页

来源 [BQ76940.pdf PDF p.35](../../sources/BQ76940.pdf#page=35)

```text
                                                                                                      bq76920, bq76930, bq76940
www.ti.com                                                                             SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015




         LOAD_PRESENT (Bit 7): Valid only when [CHG_ON] = 0. Is high if CHG pin is detected to exceed
         VLOAD_DETECT while CHG_ON = 0, which suggests that external load is present. Note this bit is read-
         only and automatically clears when load is removed.
                    0 = CHG pin < VLOAD_DETECT or [CHG_ON] = 1.
                    1 = CHG pin >VLOAD_DETECT, while [CHG_ON] = 0.

         ADC_EN (Bit 4): ADC enable command
                    0 = Disable voltage and temperature ADC readings (also disables OV protection)
                    1 = Enable voltage and temperature ADC readings (also enables OV protection)

         TEMP_SEL (Bit 3): TSx_HI and TSx_LO temperature source
                    0 = Store internal die temperature voltage reading in TSx_HI and TSx_LO
                    1 = Store thermistor reading in TSx_HI and TSx_LO (all thermistor ports)

         RSVD (Bit 2): Reserved, do not set to 1.

         SHUT_A, SHUT_B (Bits 1–0): Shutdown command from host microcontroller. Must be written in a
         specific sequence, shown below:
         Starting from: [SHUT_A] = 0, [SHUT_B] = 0
                            Write #1: [SHUT_A] = 0, [SHUT_B] = 1
                            Write #2: [SHUT_A] = 1, [SHUT_B] = 0
         Other writes cause the command to be ignored.

                                                        Table 7-8. SYS_CTRL2 (0x05)
      BIT               7                6                5          4             3              2             1              0
    NAME          DELAY_DIS          CC_EN            CC_          RSVD          RSVD           RSVD         DSG_ON        CHG_ON
                                                    ONESHOT
    RESET               0                0                0          0             0              0             0              0
   ACCESS              RW              RW                RW         RW            RW             RW            RW             RW

         DELAY_DIS (Bit 7): Disable OV, UV, OCD, and SCD delays for faster customer production testing.
                    0 = Normal delay settings
                    1 = OV, UV, OCD, and SCD delay circuit is bypassed, creating zero delay (approximately
                        250 ms).

         CC_EN (Bit 6): Coulomb counter continuous operation enable command. If set high, [CC_ONESHOT] bit
         is ignored.
                    0 = Disable CC continuous readings
                    1 = Enable CC continuous readings and ignore [CC_ONESHOT] state

         CC_ONESHOT (Bit 5): Coulomb counter single 250-ms reading trigger command. If set to 1, the
         coulomb counter will be activated for a single 250-ms reading, and then turned back off.
         [CC_ONESHOT] will also be cleared at the conclusion of this reading, while [CC_READY] bit will be set
         to 1.
                    0 = No action
                    1 = Enable single CC reading (only valid if [CC_EN] = 0), and [CC_READY] = 0)

         RSVD (Bit 4–2): Reserved. Do not use.



Copyright © 2013–2015, Texas Instruments Incorporated                                                       Detailed Description    35
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 36 页

来源 [BQ76940.pdf PDF p.36](../../sources/BQ76940.pdf#page=36)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                www.ti.com



        DSG_ON (Bit 1): Discharge FET driver (low side NCH) or discharge signal control
                  0 = DSG is off.
                  1 = DSG is on.

        CHG_ON (Bit 0): Discharge FET driver (low side NCH) or discharge signal control
                 0 = CHG is off.
                 1 = CHG is on.

                                               Table 7-9. PROTECT1 (0x06)
      BIT             7               6             5          4             3                   2                1                0
     NAME          RSNS               —        RSVD         SCD_D1         SCD_D0            SCD_T2           SCD_T1           SCD_T0
     RESET            0               0             0          0             0                   0                0                0
     ACCESS          RW               R         RW            RW            RW                  RW              RW               RW

        RSNS (Bit 7): Allows for doubling the OCD and SCD thresholds simultaneously
                  0 = OCD and SCD thresholds at lower input range
                  1 = OCD and SCD thresholds at upper input range

        RSVD (Bit 5): Reserved, do not set to 1.

        SCD_D1:0 (Bits 4–3):
        Short circuit in discharge delay setting. A 400-µs setting is recommended only in systems using
        maximum cell measurement input resistance, Rc, of 1 kΩ (which corresponds to minimum internal cell
        balancing current or external cell balancing configuration).


                                             Code                            (in µs)
                                              0x0                                70
                                              0x1                                100
                                              0x2                                200
                                              0x3                                400

        SCD_T2:0 (Bits 2–0): Short circuit in discharge threshold setting


                               Code                     RSNS = 1 (in mV)                     RSNS = 0 (in mV)
                               0x0                             44                                    22
                               0x1                             67                                    33
                               0x2                             89                                    44
                               0x3                            111                                    56
                               0x4                            133                                    67
                               0x5                            155                                    78
                               0x6                            178                                    89
                               0x7                            200                                    100


                                               Table 7-10. PROTECT2 (0x07)
      BIT             7               6             5          4             3                   2                1                0
     NAME            —          OCD_D2        OCD_D1        OCD_D0         OCD_T3            OCD_T2           OCD_T1          OCD_T0
     RESET            0               0             0          0             0                   0                0                0
     ACCESS           R              RW         RW            RW            RW                  RW              RW               RW



36      Detailed Description                                                           Copyright © 2013–2015, Texas Instruments Incorporated
                                                  Submit Documentation Feedback
                                          Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 37 页

来源 [BQ76940.pdf PDF p.37](../../sources/BQ76940.pdf#page=37)

```text
                                                                                                         bq76920, bq76930, bq76940
www.ti.com                                                                                SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015




         OCD_D2:0 (Bits 6–4): Overcurrent in discharge delay setting


                                                    Code                              (in ms)
                                                        0x0                               8
                                                        0x1                               20
                                                        0x2                               40
                                                        0x3                               80
                                                        0x4                               160
                                                        0x5                               320
                                                        0x6                               640
                                                        0x7                            1280

         OCD_T3:0 (Bits 3–0): Overcurrent in discharge threshold setting.


                                  Code                            RSNS = 1 (in mV)                (RSNS = 0 (in mV)
                                   0x0                                  17                                8
                                   0x1                                  22                                11
                                   0x2                                  28                                14
                                   0x3                                  33                                17
                                   0x4                                  39                                19
                                   0x5                                  44                                22
                                   0x6                                  50                                25
                                   0x7                                  56                                28
                                   0x8                                  61                                31
                                   0x9                                  67                                33
                                  0xA                                   72                                36
                                  0xB                                   78                                39
                                  0xC                                   83                                42
                                  0xD                                   89                                44
                                  0xE                                   94                                47
                                   0xF                                  100                               50


                                                        Table 7-11. PROTECT3 (0x08)
      BIT               7                6                    5          4            3              2                1            0
    NAME             UV_D1           UV_D0              OV_D1         OV_D0          RSVD          RSVD          RSVD            RSVD
    RESET               0                0                    0          0            0              0                0            0
   ACCESS              RW                RW               RW            RW           RW             RW             RW             RW

         UV_D1:0 (Bits 7–6): Undervoltage delay setting


                                                    Code                               (in s)
                                                        0x0                               1
                                                        0x1                               4
                                                        0x2                               8
                                                        0x3                               16




Copyright © 2013–2015, Texas Instruments Incorporated                                                           Detailed Description    37
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 38 页

来源 [BQ76940.pdf PDF p.38](../../sources/BQ76940.pdf#page=38)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                           www.ti.com




        OV_D1:0 (Bits 5–4): Overvoltage delay setting


                                           Code                            (in s)
                                            0x0                               1
                                            0x1                               2
                                            0x2                               4
                                            0x3                               8

        RSVD (Bits 3–0): These bits are for TI internal debug use only and must be configured to the default
        settings indicated.

                                              Table 7-12. OV_TRIP (0x09)
      BIT             7           6               5         4             3                 2                1                0
     NAME          OV_T7       OV_T6         OV_T5        OV_T4         OV_T3            OV_T2            OV_T1           OV_T0
     RESET            1           0               1         0             1                 1                0                0
     ACCESS          RW         RW            RW           RW            RW                RW              RW               RW

        OV_T7:0 (Bits 7–0): Middle 8 bits of the direct ADC mapping of the desired OV protection threshold,
        with upper 2 MSB set to 10 and lower 2 LSB set to 1000. The equivalent OV threshold is mapped to:
        10-OV_T<7:0>1000.

        By default, OV_TRIP is configured to a 0xAC setting.

        Note that OV_TRIP is based on the ADC voltage, which requires back-calculation using the GAIN and
        OFFSET values stored in ADCGAIN<4:0>and ADCOFFSET<7:0>.

                                              Table 7-13. UV_TRIP (0x0A)
      BIT             7           6               5         4             3                 2                1                0
     NAME          UV_T7       UV_T6         UV_T5        UV_T4         UV_T3            UV_T2            UV_T1           UV_T0
     RESET            1           0               0         1             0                 1                1                1
     ACCESS          RW         RW            RW           RW            RW                RW              RW               RW

        UV_T7:0 (Bits 7–0): Middle 8 bits of the direct ADC mapping of the desired UV protection threshold, with
        upper 2 MSB set to 01 and lower 4 LSB set to 0000. In other words, the equivalent OV threshold is
        mapped to: 01-UV_T<7:0>–0000.

        By default, UV_TRIP is configured to a 0x97 setting. .

        Note that UV_TRIP is based on the ADC voltage, which requires back-calculation using the GAIN and
        OFFSET values stored in ADCGAIN<4:0>and ADCOFFSET<7:0>.

                                       Table 7-14. CC_CFG REGISTER (0x0B)
      BIT             7           6               5         4             3                 2                1                0
     NAME            —           —         CC_CFG5      CC_CFG4       CC_CFG3          CC_CFG2          CC_CFG1          CC_CFG0
     RESET            0           0               0         0             0                 0                0                0
     ACCESS           R          R            RW           RW            RW                RW              RW               RW




38      Detailed Description                                                      Copyright © 2013–2015, Texas Instruments Incorporated
                                               Submit Documentation Feedback
                                       Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 39 页

来源 [BQ76940.pdf PDF p.39](../../sources/BQ76940.pdf#page=39)

```text
                                                                                                        bq76920, bq76930, bq76940
www.ti.com                                                                              SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015




         CC_CFG5:0 (Bits 5–0): For optimal performance, these bits should be programmed to 0x19 upon device
         startup.

7.5.2       Read-Only Registers

                                               Table 7-15. CELL VOLTAGE REGISTERS
VC1_HI, _LO (0x0C–0x0D), VC2_HI, _LO (0x0E–0x0F), VC3_HI, _LO (0x10–0x11), VC4_HI, _LO (0x12–0x13), VC5_HI, _LO
(0x14–0x15) / bq76930, bq76940: VC6_HI, _LO (0x16–0x17), VC7_HI, _LO (0x18–0x19), VC8_HI, _LO (0x1A–0x1B), VC9_HI, _LO
(0x1C–0x1D), VC10_HI, _LO (0x1E–0x1F) / bq76940: VC11_HI, _LO (0x20–0x21), VC12_HI, _LO (0x22–0x23), VC13_HI, _LO
(0x24–0x25), VC14_HI, _LO (0x26–0x27), VC15_HI, _LO (0x28–0x29)
      BIT               7                6               5           4             3               2             1              0
    NAME               —                —               D13         D12           D11             D10            D9             D8
    RESET               0                0               0           0             0               0             0              0
    NAME               D7               D6              D5           D4            D3              D2            D1             D0
    RESET               0                0               0           0             0               0             0              0

         D11:8 (Bits 3–0): Cell “x” ADC reading, upper 6 MSB. Always returned as an atomic value if both high
         and low registers are read in the same transaction (using address auto-increment).

         D7:0 (Bits 7–0): Cell ”x” ADC reading, lower 8 LSB.

                                          Table 7-16. BAT_HI (0x2A) and BAT_LO (0x2B)
      BIT               7                6               5           4             3               2             1              0
    NAME              D15              D14              D13         D12           D11             D10            D9             D8
    RESET               0                0               0           0             0               0             0              0
    NAME               D7               D6              D5           D4            D3              D2            D1             D0
    RESET               0                0               0           0             0               0             0              0

         D15:8 (Bits 7–0): BAT calculation based on adding up Cells 1–15, upper 8 MSB. Always returned as an
         atomic value if both high and low registers are read in the same transaction (using address auto-
         increment).

         D7:0 (Bits 7–0): BAT calculation based on adding up Cells 1–15, lower 8 LSB

                                             Table 7-17. TS1_HI (0x2C) and TS1_LO (0x2D)
      BIT               7                6               5           4             3               2             1              0
    NAME               —                —               D13         D12           D11             D10            D9             D8
    RESET               0                0               0           0             0               0             0              0
    NAME               D7               D6              D5           D4            D3              D2            D1             D0
    RESET               0                0               0           0             0               0             0              0

         D11:8 (Bits 3–0): TS1 or DIETEMP ADC reading, upper 6 MSB. Always returned as an atomic value if
         both high and low registers are read in the same transaction (using address auto-increment).

         D7:0 (Bits 7–0): TS1 or DIETEMP ADC reading, lower 8 LSB




Copyright © 2013–2015, Texas Instruments Incorporated                                                        Detailed Description    39
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 40 页

来源 [BQ76940.pdf PDF p.40](../../sources/BQ76940.pdf#page=40)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                           www.ti.com



                                      Table 7-18. TS2_HI (0x2E) and TS2_LO (0x2F)
      BIT             7           6               5            4             3               2               1                0
     NAME            —            —             D13           D12           D11             D10             D9               D8
     RESET            0           0               0            0             0               0               0                0
     NAME            D7          D6               D5           D4            D3             D2              D1               D0
     RESET            0           0               0            0             0               0               0                0

        D11:8 (Bits 3–0): TS2 ADC reading, upper 6 MSB. Always returned as an atomic value if both high and
        low registers are read in the same transaction (using address auto-increment).

        D7:0 (Bits 7–0): TS2 ADC reading, lower 8 LSB

                                      Table 7-19. TS3_HI (0x30) and TS3_LO (0x31)
      BIT             7           6               5            4             3               2               1                0
     NAME            —            —             D13           D12           D11             D10             D9               D8
     RESET            0           0               0            0             0               0               0                0
     NAME            D7          D6               D5           D4            D3             D2              D1               D0
     RESET            0           0               0            0             0               0               0                0

        D11:8 (Bits 3–0): TS3 ADC reading, upper 6 MSB. Always returned as an atomic value if both high and
        low registers are read in the same transaction (using address auto-increment).

        D7:0 (Bits 7–0): TS3 ADC reading, lower 8 LSB

                                       Table 7-20. CC_HI (0x32) and CC_LO (0x33)
      BIT             7           6               5            4             3               2               1                0
     NAME           CC15        CC14            CC13         CC12          CC11            CC10            CC9              CC8
     RESET            0           0               0            0             0               0               0                0
     NAME           CC7          CC6            CC5           CC4           CC3             CC2            CC1              CC0
     RESET            0           0               0            0             0               0               0                0

        CC15:8 (Bits 7–0): Coulomb counter upper 8 MSB. Always returned as an atomic value if both high and
        low registers are read in the same transaction (using address auto-increment).

        CC7:0 (Bits 7–0): Coulomb counter lower 8 LSB

                                               Table 7-21. ADCGAIN1 (0x50)
      BIT             7           6               5            4             3               2               1                0
     NAME            —            —               —            —         ADCGAIN4         ADCGAIN3          —                —
     RESET           —            —               —            —             —               —              —                —
     ACCESS           R           R               R            R             R               R               R               R


                                               Table 7-22. ADCGAIN2 (0x59)
      BIT             7           6               5            4             3               2               1                0
     NAME        ADCGAIN2      ADCGAIN1      ADCGAIN0          —             —               —              —                —
     RESET           —            —               —            —             —               —              —                —
     ACCESS           R           R               R            R             R               R               R               R

        ADCGAIN4:3 (Bits 3–2, address 0x50):
                                ADC GAIN
                                offset upper 2 MSB

40      Detailed Description                                                      Copyright © 2013–2015, Texas Instruments Incorporated
                                                  Submit Documentation Feedback
                                          Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 41 页

来源 [BQ76940.pdf PDF p.41](../../sources/BQ76940.pdf#page=41)

```text
                                                                                                       bq76920, bq76930, bq76940
www.ti.com                                                                             SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



         ADCGAIN2:0 (Bits 7–5, address 0x59):
                                      ADC GAIN offset lower 3 LSB

                                      ADCGAIN<4:0> is a production-trimmed value for the ADC transfer function, in
                                      units of µV/LSB. The range is 365 µV/LSB to 396 µV/LSB, in steps of 1 µV/LSB,
                                      and may be calculated as follows:

                                      GAIN = 365 µV/LSB + (ADCGAIN<4:0>in decimal) × (1 µV/LSB)

                                      Alternately, a conversion table is provided below:


                            ADC GAIN                Gain (µV/LSB)             ADC GAIN                 Gain (µV/LSB)
                              0x00                         365                  0x10                       381
                              0x01                         366                  0x11                       382
                              0x02                         367                  0x12                       383
                              0x03                         368                  0x13                       384
                              0x04                         369                  0x14                       385
                              0x05                         370                  0x15                       386
                              0x06                         371                  0x16                       387
                              0x07                         372                  0x17                       388
                              0x08                         373                  0x18                       389
                              0x09                         374                  0x19                       390
                              0x0A                         375                  0x1A                       391
                              0x0B                         376                  0x1B                       392
                              0x0C                         377                  0x1C                       393
                              0x0D                         378                  0x1D                       394
                              0x0E                         379                  0x1E                       395
                              0x0F                         380                  0x1F                       396


                                                        Table 7-23. ADCOFFSET (0x51)
      BIT               7                6                 5           4           3               2               1              0
    NAME             ADC               ADC                ADC         ADC        ADC              ADC           ADC             ADC
                   OFFSET7           OFFSET6            OFFSET5     OFFSET4    OFFSET3          OFFSET2       OFFSET1         OFFSET0
    RESET              —                —                 —           —            —              —               —               —
   ACCESS               R               R                 R           R            R              R                R              R

         ADCOFFSET7:0 (Bits 7–0):
                                      ADC offset, stored in 2’s complement format in mV units. Positive full-scale range
                                      corresponds to 0x7F and negative full-scale corresponds to 0x80. The full-scale
                                      input range is –128 mV to 127 mV, with an LSB of 1 mV.

                                      The table below shows example offsets.


                                                ADCOFFSET                        Offset (mV)
                                                    0x00                                0
                                                    0x01                                1
                                                    0x7F                               127
                                                    0x80                               –128
                                                    0x81                               –127
                                                    0xFF                               –1



Copyright © 2013–2015, Texas Instruments Incorporated                                                          Detailed Description     41
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 42 页

来源 [BQ76940.pdf PDF p.42](../../sources/BQ76940.pdf#page=42)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                            www.ti.com



8 Application and Implementation

                                                                NOTE
                     Information in the following application section is not part of the TI component specification,
                     and TI does not warrant its accuracy or completeness. TI’s customers are responsible for
                     determining suitability of components for their purposes. Customers should validate and test
                     their design implementation to confirm system functionality.


8.1     Application Information
        The bq769x0 family of battery monitoring AFEs enabling cell parametric measurement and protection is a
        variety of 3-series to 15-series Li-Ion/Li Polymer battery packs.
        To evaluate the performance and configurations of the device users need the bq76940/bq76930/bq76920
        Evaluation Software, (SLUCC539) tool to configure the internal registers for a specific battery pack and
        application. The Evaluation Software tool is a graphical user-interface tool installed on a PC during
        development. This can be used in conjunction with the bq76920EVM, bq76930EVM or bq76940EVM.
        The bq769x0 devices are expected to be implemented in a system with a microcontroller that can perform
        additional functions based on the data made collected. The bq78350 is one example of a companion to
        the bq769x0 family.

8.1.1    Configuring Alternative Cell Counts
        Each bq769x0 family of IC's support a variety of cell counts. The following tables provide guidance on
        which device and which input pins to use, depending on the number of cells in the pack.


                                         Table 8-1. Cell Connections for bq76920
                       Cell Input                 3 Cells                4 Cells                   5 Cells
                        VC5–VC4                   CELL 3                 CELL 4                    CELL 5
                        VC4–VC3                    short                  short                    CELL 4
                        VC3–VC2                    short                 CELL 3                    CELL 3
                        VC2–VC1                   CELL 2                 CELL 2                    CELL 2
                        VC1–VC0                   CELL 1                 CELL 1                    CELL 1


                                         Table 8-2. Cell Connections for bq76930
      Cell Input              6 Cells              7 Cells              8 Cells               9 Cells                 10 Cells
      VC10–VC9                CELL 6               CELL 7              CELL 8                 CELL 9                  CELL 10
      VC9–VC8                  short                short                short                 short                   CELL 9
      VC8–VC7                  short                short              CELL 7                 CELL 8                   CELL 8
      VC7–VC6                 CELL 5               CELL 6              CELL 6                 CELL 7                   CELL 7
      VC6–VC5b                CELL 4               CELL 5              CELL 5                 CELL 6                   CELL 6
      VC5–VC4                 CELL 3               CELL 4              CELL 4                 CELL 5                   CELL 5
      VC4–VC3                  short                short                short                CELL 4                   CELL 4
      VC3–VC2                  short               CELL 3              CELL 3                 CELL 3                   CELL 3
      VC2–VC1                 CELL 2               CELL 2              CELL 2                 CELL 2                   CELL 2
      VC1–VC0                 CELL 1               CELL 1              CELL 1                 CELL 1                   CELL 1




42      Application and Implementation                                             Copyright © 2013–2015, Texas Instruments Incorporated
                                                 Submit Documentation Feedback
                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 43 页

来源 [BQ76940.pdf PDF p.43](../../sources/BQ76940.pdf#page=43)

```text
                                                                                                                            bq76920, bq76930, bq76940
www.ti.com                                                                                             SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



                                                       Table 8-3. Cell Connections for bq76940
   Cell Input           9 Cells               10 Cells             11 Cells          12 Cells                 13 Cells           14 Cells               15 Cells
  VC15–VC14             CELL 9                CELL 10              CELL 11           CELL 12                  CELL 13            CELL 14                CELL 15
  VC14–VC13              short                    short             short                 short                short               short                CELL 14
  VC13–VC12              short                    short             short            CELL 11                  CELL 12            CELL 13                CELL 13
  VC12–VC11             CELL 8                   CELL 9            CELL 10           CELL 10                  CELL 11            CELL 12                CELL 12
  VC11–VC10b            CELL 7                   CELL 8            CELL 9              CELL 9                 CELL 10            CELL 11                CELL 11
   VC10–VC9             CELL 6                   CELL 7            CELL 8              CELL 8                 CELL 9             CELL 10                CELL 10
    VC9–VC8              short                    short             short                 short                short              CELL 9                CELL 9
    VC8–VC7              short                    short            CELL 7              CELL 7                 CELL 8              CELL 8                CELL 8
    VC7–VC6             CELL 5                   CELL 6            CELL 6              CELL 6                 CELL 7              CELL 7                CELL 7
   VC6–VC5b             CELL 4                   CELL 5            CELL 5              CELL 5                 CELL 6              CELL 6                CELL 6
    VC5–VC4             CELL 3                   CELL 4            CELL 4              CELL 4                 CELL 5              CELL 5                CELL 5
    VC4–VC3              short                    short             short                 short               CELL 4              CELL 4                CELL 4
    VC3–VC2              short                   CELL 3            CELL 3              CELL 3                 CELL 3              CELL 3                CELL 3
    VC2–VC1             CELL 2                   CELL 2            CELL 2              CELL 2                 CELL 2              CELL 2                CELL 2
    VC1–VC0             CELL 1                   CELL 1            CELL 1              CELL 1                 CELL 1              CELL 1                CELL 1


8.2        Typical Applications


                                                                              CAUTION
                           The external circuitries in the following schematics show minimum requirements
                           to ensure device robustness during cell connection to the PCB and normal
                           operation.


                                                                                                                                                               PACK +
                                                                                                              Rf

                                                             BAT
                                                 VC5      REGSRC
      Rc
                 Cc                              VC4      REGOUT
                                                 VC3        CAP1
                                                                               10 kΩ
      Rc                                         VC2         TS1
                 Cc                                                                           1 µF     1 µF    Cf      4.7 µF
                                                 VC1         SCL               10k
      Rc                                         VC0         SDA
                 Cc
                                                 SRP         VSS
      Rc                                         SRN        CHG
                 Cc                                                                       PUSH-BUTTON FOR BOOT
                                                 ALERT       DSG
                                                                                                                                   VCC
      Rc         Cc
                                                                                                                                   SCL
                                                                                                                                   SDA     Companion
            Cc                                                                                                                             Controller
                                                                                                                                   GPIO
      Rc                                                                             1M
                                                                                                                                   VSS

                      0.1 µF   0.1 µF   0.1 µF


                        100             100                 1M                                    1M

                               Rsns                                                                                                                            PACK–

                                  Figure 8-1. bq76920 with bq78350 Companion Controller IC




Copyright © 2013–2015, Texas Instruments Incorporated                                                                    Application and Implementation            43
                                                               Submit Documentation Feedback
                                                       Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 44 页

来源 [BQ76940.pdf PDF p.44](../../sources/BQ76940.pdf#page=44)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                               www.ti.com



                                                                                                                                                   PACK +
        Rc
                  Cc

        Rc
                  Cc

        Rc
                  Cc
                                                                                                       Rf
        Rc                                     VC10       BAT
                  Cc
                                               VC9       CAP2
        Rc                                     VC8        TS2                                          Cf
                  Cc
          VC5x                                 VC7         NC              10k           1 µF
             Cc                                VC6         NC
        Rc
                                               VC5b      VC5x                                                       A
A                                                                                                           Rf
                                               VC5     REGSRC
        Rc
                  Cc                           VC4     REGOUT
                                               VC3       CAP1
                                                                          10 kΩ
        Rc                                     VC2        TS1
                  Cc                                                                     1µF     1µF   Cf        4.7 µF
                                               VC1        SCL              10k
        Rc                                     VC0        SDA
                  Cc
                                               SRP        VSS
        Rc                                     SRN        CHG
                  Cc                                                                  PUSH-BUTTON FOR BOOT
                                               ALERT      DSG
                                                                                                                          VCC
        Rc        Cc
                                                                                                                          SCL
                                                                                                                          SDA    Companion
        Rc   Cc                                                                                                                  Controller
                                                                                                                          GPIO
                                                                                 1M
                                                                                                                          VSS

                       0.1 µF   0.1 µF   0.1 µF


                         100             100              1M                                1M

                                Rsns                                                                                                                PACK -

                                Figure 8-2. bq76930 with bq78350 Companion Controller IC




44     Application and Implementation                                                              Copyright © 2013–2015, Texas Instruments Incorporated
                                                          Submit Documentation Feedback
                                                  Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 45 页

来源 [BQ76940.pdf PDF p.45](../../sources/BQ76940.pdf#page=45)

```text
                                                                                                                                bq76920, bq76930, bq76940
www.ti.com                                                                                                 SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



                                                                                                                                                             PACK +
          Rc
                       Cc

          Rc
                       Cc

          Rc
                       Cc

          Rc
                       Cc

          Rc           Cc
          VC10x

          Rc      Cc

B                                                                                                                 Rf

          Rc
                       Cc                            VC15
                                                     VC14        BAT
          Rc
                       Cc                            VC13       CAP3
                                                     VC12        TS3                                              Cf
          Rc                                         VC11                        10k
                       Cc                                         NC                        1 µF
                                                     VC10b        NC
          Rc                                         VC10      VC10x                                                           B
                       Cc                                                                                              Rf
                                                     VC9        CAP2
          Rc                                         VC8         TS2                                              Cf
                       Cc
            VC5x                                     VC7          NC             10k        1 µF
                  Cc                                 VC6          NC
          Rc
                                                     VC5b       VC5x                                                           A
A                                                                                                                      Rf
                                                     VC5      REGSRC
          Rc
                       Cc                            VC4      REGOUT
                                                     VC3        CAP1
                                                                                 10 kΩ
          Rc                                         VC2         TS1
                       Cc                                                                          1 µF    1 µF   Cf        4.7 µF
                                                     VC1         SCL             10k
          Rc                                         VC0         SDA
                       Cc
                                                     SRP         VSS
          Rc                                         SRN        CHG
                       Cc                                                                   PUSH-BUTTON FOR BOOT
                                                     ALERT       DSG
                                                                                                                                        VCC
          Rc           Cc
                                                                                                                                        SCL
                                                                                                                                        SDA    Companion
          Rc      Cc                                                                                                                           Controller
                                                                                                                                        GPIO
                                                                                       1M
                                                                                                                                        VSS

                             0.1 µF   0.1 µF   0.1 µF


                               100             100              1M                                    1M

                                      Rsns                                                                                                                   PACK –

                                      Figure 8-3. bq76940 with bq78350 Companion Controller IC

8.2.1     Design Requirements

                                                        Table 8-4. bq769x0 Design Requirements
                             DESIGN PARAMETER                                                               EXAMPLE VALUE at TA = 25°C
                       Minimum system operating voltage                                                                        24 V
                        Cell minimum operating voltage                                                                        3.0 V
                               Series Cell Count                                                                                   8
                                Charge Voltage                                                                                33.6 V
                            Maximum Charge Current                                                                            3.0 A
                            Peak Discharge Current                                                                            10.0 A
                            OV Protection Threshold                                                                           4.30 V
                              OV Protection Delay                                                                                  2s
                            UV Protection Threshold                                                                           2.5 V


Copyright © 2013–2015, Texas Instruments Incorporated                                                                       Application and Implementation      45
                                                                 Submit Documentation Feedback
                                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 46 页

来源 [BQ76940.pdf PDF p.46](../../sources/BQ76940.pdf#page=46)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                           www.ti.com



                                 Table 8-4. bq769x0 Design Requirements (continued)
                      DESIGN PARAMETER                                           EXAMPLE VALUE at TA = 25°C
                        UV Protection Delay                                                     4s
                  OCD Protection Threshold Max                                                15 A
                    OCD Protection Delay Time                                                320 ms
                  SCD Protection Threshold Max                                                25 A
                    SCD Protection Delay Time                                                100 µs


8.2.2       Detailed Design Procedure
        To begin the design process, there are some key steps required for component selection and protection
        configuration.

8.2.2.1      Step-by-Step Design Procedure
        •    Determine the number of series cells.
             – This value depends on the cell chemistry and the load requirements of the system. For example, to
                 support a minimum battery voltage of 24 V using Li-CO2 type cells with a cell minimum voltage of
                 3.0 V, there needs to be at least 8-series cells.
        •    Select the correct bq769x0 device.
             – For 8-series cells, the bq76930 is needed.
             – For the correct cell connections, see Table 8-2.
        •    Select the correct protection FETs.
             – The bq76930 uses a low-side drive suitable for N-CH FETs.
             – These FETs should be rated for the maximum:
                 • Voltage, which should be approximately 5 V (DC) 10 V (peak) per series cell: for example, 40 V.
                 • Current, which should be calculated based on both the maximum DC current and the maximum
                     transient current with some margin: for example, 30 A.
                 • Power Dissipation, which can be a factor of the RDS(ON) rating of the FET, the FET package,
                     and the PCB design: for example, 5 W, assuming 5 mΩ RDS(ON).
        •    Select the correct sense resistor.
             – The resistance value should be selected to maximize the input bandwidth use of the coulomb
                 counter range, CCRANGE, but not exceed the absolute maximum ratings.
                 • Using the normal max discharge current, RSNS = 200 mV / 10.0 A = 20 mΩ.
                 • However, considering ISCD of 25 A and Abs Max SRP–SRN input of –300 mV, RSNS = 300 mV
                     / 25 A = 7.5 mΩ
                 • The maximum operating current of the system should also be considered as this should be
                     below the maximum OCD Threshold, which with a RSNS of 7.5 mΩ gives a max OCD current
                     setting of 13.3 A.
             – Further tolerance analysis (value tolerance, temperature variation, and so on) and PCB design
                 margin should also be considered, so RSNS of 5 mΩ would be suitable with a 75-ppm temperature
                 coefficient and power rating of 5 W.
        •    The bq76930 is chosen, and so the REGSRC pin needs to be powered through a source follower
             circuit where the FET is used to provide current for REGSRC from the battery positive terminal while
             reducing the voltage to a suitable value for the IC.
             – The FET also dissipates the power resulting from the load current and dropped voltage external to
                 the IC and care should be taken to ensure the correct dissipation ratings are specified by the
                 chosen FET.




46      Application and Implementation                                            Copyright © 2013–2015, Texas Instruments Incorporated
                                                  Submit Documentation Feedback
                                          Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 47 页

来源 [BQ76940.pdf PDF p.47](../../sources/BQ76940.pdf#page=47)

```text
                                                                                                   bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



        •    Configure the Current-based protection settings through PROTECT1 and PROTECT2:
             – Ideal SCD Threshold = 25 A × 5 mΩ = 125 mV.
               • However, the closest options are 111 mV (0x03) and 133 mV (0x04) providing 22.2 A and 26.6
                   A, respectively. Both options have the RSNS bit = 1.
               • 0x03 (22.2 A) will be used in this example.
             – The SCD delay threshold setting for a 100 µs delay is 0x01.
             – Therefore, PROTECT1 should be programmed with 0x8C.
             – Ideal OCD Threshold = 15 A × 5 mΩ = 75 mV.
               • However, the closest options are 72 mV (0x0A) and 78 mV (0x0B), providing 14.4 A and 15.6 A,
                   respectively. Both options have the RSNS bit = 1.
               • 0x0A (14,4A) will be used in this example.
             – The OCD delay threshold setting for a 320-ms delay is 0x05.
             – Therefore, PROTECT2 should be programmed with 0x5B.

                                                                      NOTE
                        Care should be taken when determining the setting of OV_TRIP and UV_TRIP as these are
                        ADC value outputs and correlation to cell voltage also requires consideration of the ADC
                        GAIN and ADC OFFSET registers. More specific details can be found in Section 7.3.1.2.

        •    Configure the Voltage-based protection settings through OV_TRIP, UV_TRIP and PROTECT3:
             – The selected OV Threshold is 4.30 V.
               • Therefore, OV_TRIP should be programmed with 0xC9.
             – The selected UV Threshold is 2.5 V.
               • Therefore, UV_TRIP should be programmed with 0x1A.
             – The selected OV Delay is 2 s and the selected UV Delay is 4 s.
               • Therefore, PROTECT3 should be programmed with 0x50.




Copyright © 2013–2015, Texas Instruments Incorporated                                            Application and Implementation   47
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 48 页

来源 [BQ76940.pdf PDF p.48](../../sources/BQ76940.pdf#page=48)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                                                 www.ti.com



8.2.3                    Application Curves

                       0.020                                                                            0.0
                                                                      VC1 Error
                       0.018
                                                                      VC2 Error                        ±0.2
                       0.016
                       0.014                                          VC3 Error                        ±0.4
                       0.012                                          VC4 Error
     VCx Error (mV)




                                                                                                       ±0.6




                                                                                         Offset (uV)
                       0.010                                          VC5 Error
                       0.008                                                                           ±0.8
                       0.006
                                                                                                       ±1.0
                       0.004
                       0.002                                                                           ±1.2
                       0.000
                                                                                                       ±1.4
                      ±0.002
                      ±0.004                                                                           ±1.6
                            2.00 2.30 2.60 2.90 3.20 3.50 3.80 4.10 4.40 4.70 5.00                            ±40        ±15         10         35         60         85
                                                 VCx Input (V)                    C001                                             Temperature (ƒC)                   C002




      Figure 8-4. bq76930 VCx Error Across Input Range @ 25°C                                                       Figure 8-5. Coulomb Counter Offset




48                      Application and Implementation                                                                  Copyright © 2013–2015, Texas Instruments Incorporated
                                                                     Submit Documentation Feedback
                                                             Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 49 页

来源 [BQ76940.pdf PDF p.49](../../sources/BQ76940.pdf#page=49)

```text
                                                                                                    bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



9 Power Supply Recommendations
        The bq769x0 devices are powered through the BAT and REGSRC pins but the bq76930 and bq76940
        have additional ‘Power’ pins to provide the power to the entire device in the higher cell configurations.
        The use of Rf and Cf connected to the BAT pin, noted in the typical application diagrams, are required to
        filter system transients from disturbing the device power supply. These components should be placed as
        close as to the IC as possible.
        Additionally, for the bq76930 and bq76940 there are additional requirements to ensure a stable power
        supply to the device. The REGSRC pin is powered through a source follower circuit where the FET is
        used to provide current for REGSRC from the battery positive terminal while reducing the voltage to a
        suitable value for the IC. The FET also dissipates the power resulting from the load current and dropped
        voltage external to the IC and care should be taken to ensure the correct dissipation ratings are specified
        by the chosen FET.
        The bq76920 does not use a FET because the battery voltage is within the REGSRC range.




Copyright © 2013–2015, Texas Instruments Incorporated                                           Power Supply Recommendations     49
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 50 页

来源 [BQ76940.pdf PDF p.50](../../sources/BQ76940.pdf#page=50)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                                   www.ti.com



10 Layout
10.1 Layout Guidelines
       It is strongly recommended for best measurement performance to keep high current signals from
       interfering with the measurement system inputs and ground.
       A second key recommendation is to ensure that the bq769x0 input filtering capacitors and power
       capacitors are connected to a common ground with as little parasitic resistance between the connections
       as possible.

10.2 Layout Example
       Figure 10-1 shows a guideline of how to place key components compared to respective ground zones,
       based on the bq76920/30/40 EVMs.


                                            Low Current Ground Plane Available Area




                                                                        bq78350
                                             bq769x0




                                                            Key components placed here
                                                                      bq78350
                                                                  bq76920/30/40
                                                                Measurement Filter
                                        Ground                Resistors and Capacitors
                                     interconnect


                           BAT-              RSNS                        DSG        CHG                PACK-

                                                       Key components placed here
                                                             Protection FETs
                                                             Sense Resistor                            PACK+
                           BAT+
                                             High Current Ground Plane Available Area



                  Figure 10-1. System Component Placement Layout vs. Ground Zone Guide

                                                                  CAUTION
                      Care should be taken when placing key power pin capacitors to minimize PCB
                      trace impedances. These impedances could result in device resets or other
                      unexpected operations when the device is at peak power consumption.
                      Although not shown in the diagrams, this caution also applies to the resistor
                      and capacitor network surrounding the current sense resistor.




50     Layout                                                                             Copyright © 2013–2015, Texas Instruments Incorporated
                                               Submit Documentation Feedback
                                       Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 51 页

来源 [BQ76940.pdf PDF p.51](../../sources/BQ76940.pdf#page=51)

```text
                                                                                                    bq76920, bq76930, bq76940
www.ti.com                                                                           SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



                                                                       BATTERY–




                                    bq76920

                                         REGSRC

                                         REGOUT

                                               VC1

                                              VC0
                                                                                           PCB Trace impedance

                                              VSS




                                                                        SENSE
                                                                       RESISTOR




                                                  Figure 10-2.
                    Good Layout: Input Capacitor Grounding with Low Parasitic PCB Impedance




Copyright © 2013–2015, Texas Instruments Incorporated                                                                 Layout     51
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 52 页

来源 [BQ76940.pdf PDF p.52](../../sources/BQ76940.pdf#page=52)

```text
bq76920, bq76930, bq76940
SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015                                                                          www.ti.com



                                                                BATTERY–




                              bq76920

                                  REGSRC

                                  REGOUT

                                        VC1

                                        VC0
                                                                                    PCB Trace impedance

                                        VSS




                                                                 SENSE
                                                                RESISTOR




                                               Figure 10-3.
                Weak Layout: Input Capacitor Grounding with High Parasitic PCB Impedance




52     Layout                                                                    Copyright © 2013–2015, Texas Instruments Incorporated
                                                 Submit Documentation Feedback
                                         Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 53 页

来源 [BQ76940.pdf PDF p.53](../../sources/BQ76940.pdf#page=53)

```text
                                                                                                         bq76920, bq76930, bq76940
www.ti.com                                                                                 SLUSBK2F – OCTOBER 2013 – REVISED NOVEMBER 2015



11 Device and Documentation Support
11.1 Documentation Support

11.1.1 Related Documentation
        For related documentation, see the following: bq76920 Evaluation Module User 's Guide (SLVU924), and
        bq76920, bq76930, bq76940 AFE FAQ (SLUUB41).

11.2 Related Links
        The table below lists quick access links. Categories include technical documents, support and community
        resources, tools and software, and quick access to sample or buy.

                                                          Table 11-1. Related Links
                                                                            TECHNICAL                 TOOLS &            SUPPORT &
        PARTS             PRODUCT FOLDER                SAMPLE & BUY
                                                                            DOCUMENTS                SOFTWARE            COMMUNITY
       bq76920                  Click here                Click here          Click here              Click here           Click here
       bq76930                  Click here                Click here          Click here              Click here           Click here
       bq76940                  Click here                Click here          Click here              Click here           Click here



11.2.1 Community Resources
        The following links connect to TI community resources. Linked contents are provided "AS IS" by the
        respective contributors. They do not constitute TI specifications and do not necessarily reflect TI's views;
        see TI's Terms of Use.
        TI E2E™ Online Community TI's Engineer-to-Engineer (E2E) Community. Created to foster
                   collaboration among engineers. At e2e.ti.com, you can ask questions, share knowledge,
                   explore ideas and help solve problems with fellow engineers.
        TI Embedded Processors Wiki Texas Instruments Embedded Processors Wiki. Established to help
                   developers get started with Embedded Processors from Texas Instruments and to foster
                   innovation and growth of general knowledge about the hardware and software surrounding
                   these devices.

11.3 Trademarks
        E2E is a trademark of Texas Instruments.
        I2C is a trademark of NXP B.V. Corporation.
        All other trademarks are the property of their respective owners.

11.4 Electrostatic Discharge Caution
          This integrated circuit can be damaged by ESD. Texas Instruments recommends that all integrated circuits be handled with
          appropriate precautions. Failure to observe proper handling and installation procedures can cause damage.
          ESD damage can range from subtle performance degradation to complete device failure. Precision integrated circuits may be more
          susceptible to damage because very small parametric changes could cause the device not to meet its published specifications.


11.5 Glossary
        TI Glossary This glossary lists and explains terms, acronyms, and definitions.

12 Mechanical, Packaging, and Orderable Information
        The following pages include mechanical, packaging, and orderable information. This information is the
        most current data available for the designated devices. This data is subject to change without notice and
        revision of this document. For browser-based versions of this data sheet, refer to the left-hand navigation.


Copyright © 2013–2015, Texas Instruments Incorporated                             Mechanical, Packaging, and Orderable Information      53
                                                        Submit Documentation Feedback
                                                Product Folder Links: bq76920 bq76930 bq76940
```

## PDF 第 54 页

来源 [BQ76940.pdf PDF p.54](../../sources/BQ76940.pdf#page=54)

```text
                                                                                                                   PACKAGE OPTION ADDENDUM


www.ti.com                                                                                                                                                  30-Mar-2016


PACKAGING INFORMATION

       Orderable Device   Status   Package Type Package Pins Package    Eco Plan      Lead/Ball Finish     MSL Peak Temp       Op Temp (°C)       Device Marking          Samples
                            (1)                 Drawing        Qty          (2)              (6)                 (3)                                      (4/5)

         BQ7692000PW      ACTIVE     TSSOP       PW     20     70      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692000
                                                                        & no Sb/Br)
        BQ7692000PWR      ACTIVE     TSSOP       PW     20    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692000
                                                                        & no Sb/Br)
         BQ7692003PW      ACTIVE     TSSOP       PW     20     70      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692003
                                                                        & no Sb/Br)
        BQ7692003PWR      ACTIVE     TSSOP       PW     20    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692003
                                                                        & no Sb/Br)
         BQ7692006PW      ACTIVE     TSSOP       PW     20     70      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692006
                                                                        & no Sb/Br)
        BQ7692006PWR      ACTIVE     TSSOP       PW     20    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7692006
                                                                        & no Sb/Br)
        BQ7693000DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693000
                                                                        & no Sb/Br)
        BQ7693000DBTR     ACTIVE     TSSOP       DBT    30    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693000
                                                                        & no Sb/Br)
        BQ7693001DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693001
                                                                        & no Sb/Br)
        BQ7693001DBTR     ACTIVE     TSSOP       DBT    30    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693001
                                                                        & no Sb/Br)
        BQ7693002DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693002
                                                                        & no Sb/Br)
        BQ7693002DBTR     ACTIVE     TSSOP       DBT    30    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693002
                                                                        & no Sb/Br)
        BQ7693003DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693003
                                                                        & no Sb/Br)
        BQ7693003DBTR     ACTIVE     TSSOP       DBT    30    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693003
                                                                        & no Sb/Br)
        BQ7693006DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693006
                                                                        & no Sb/Br)
        BQ7693006DBTR     ACTIVE     TSSOP       DBT    30    2000     Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693006
                                                                        & no Sb/Br)
        BQ7693007DBT      ACTIVE     TSSOP       DBT    30     60      Green (RoHS        CU NIPDAU      Level-2-260C-1 YEAR     -40 to 85    BQ7693007
                                                                        & no Sb/Br)



                                                                        Addendum-Page 1
```

## PDF 第 55 页

来源 [BQ76940.pdf PDF p.55](../../sources/BQ76940.pdf#page=55)

```text
                                                                                                                                                 PACKAGE OPTION ADDENDUM


www.ti.com                                                                                                                                                                                      30-Mar-2016



        Orderable Device            Status    Package Type Package Pins Package              Eco Plan         Lead/Ball Finish         MSL Peak Temp         Op Temp (°C)               Device Marking        Samples
                                       (1)                 Drawing        Qty                    (2)                  (6)                      (3)                                            (4/5)

        BQ7693007DBTR               ACTIVE        TSSOP          DBT       30     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7693007
                                                                                            & no Sb/Br)
         BQ7694000DBT               ACTIVE        TSSOP          DBT       44      40      Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694000
                                                                                            & no Sb/Br)
        BQ7694000DBTR               ACTIVE        TSSOP          DBT       44     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694000
                                                                                            & no Sb/Br)
         BQ7694001DBT               ACTIVE        TSSOP          DBT       44      40      Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694001
                                                                                            & no Sb/Br)
        BQ7694001DBTR               ACTIVE        TSSOP          DBT       44     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694001
                                                                                            & no Sb/Br)
         BQ7694002DBT               ACTIVE        TSSOP          DBT       44      40      Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694002
                                                                                            & no Sb/Br)
        BQ7694002DBTR               ACTIVE        TSSOP          DBT       44     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694002
                                                                                            & no Sb/Br)
         BQ7694003DBT               ACTIVE        TSSOP          DBT       44      40      Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694003
                                                                                            & no Sb/Br)
        BQ7694003DBTR               ACTIVE        TSSOP          DBT       44     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694003
                                                                                            & no Sb/Br)
         BQ7694006DBT               ACTIVE        TSSOP          DBT       44      40      Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694006
                                                                                            & no Sb/Br)
        BQ7694006DBTR               ACTIVE        TSSOP          DBT       44     2000     Green (RoHS           CU NIPDAU           Level-2-260C-1 YEAR       -40 to 85          BQ7694006
                                                                                            & no Sb/Br)

(1)
   The marketing status values are defined as follows:
ACTIVE: Product device recommended for new designs.
LIFEBUY: TI has announced that the device will be discontinued, and a lifetime-buy period is in effect.
NRND: Not recommended for new designs. Device is in production to support existing customers, but TI does not recommend using this part in a new design.
PREVIEW: Device has been announced but is not in production. Samples may or may not be available.
OBSOLETE: TI has discontinued the production of the device.

(2)
   Eco Plan - The planned eco-friendly classification: Pb-Free (RoHS), Pb-Free (RoHS Exempt), or Green (RoHS & no Sb/Br) - please check http://www.ti.com/productcontent for the latest availability
information and additional product content details.
TBD: The Pb-Free/Green conversion plan has not been defined.
Pb-Free (RoHS): TI's terms "Lead-Free" or "Pb-Free" mean semiconductor products that are compatible with the current RoHS requirements for all 6 substances, including the requirement that
lead not exceed 0.1% by weight in homogeneous materials. Where designed to be soldered at high temperatures, TI Pb-Free products are suitable for use in specified lead-free processes.
Pb-Free (RoHS Exempt): This component has a RoHS exemption for either 1) lead-based flip-chip solder bumps used between the die and package, or 2) lead-based die adhesive used between
the die and leadframe. The component is otherwise considered Pb-Free (RoHS compatible) as defined above.




                                                                                             Addendum-Page 2
```

## PDF 第 56 页

来源 [BQ76940.pdf PDF p.56](../../sources/BQ76940.pdf#page=56)

```text
                                                                                                                                                     PACKAGE OPTION ADDENDUM


www.ti.com                                                                                                                                                                                          30-Mar-2016


Green (RoHS & no Sb/Br): TI defines "Green" to mean Pb-Free (RoHS compatible), and free of Bromine (Br) and Antimony (Sb) based flame retardants (Br or Sb do not exceed 0.1% by weight
in homogeneous material)

(3)
      MSL, Peak Temp. - The Moisture Sensitivity Level rating according to the JEDEC industry standard classifications, and peak solder temperature.

(4)
      There may be additional marking, which relates to the logo, the lot trace code information, or the environmental category on the device.

(5)
   Multiple Device Markings will be inside parentheses. Only one Device Marking contained in parentheses and separated by a "~" will appear on a device. If a line is indented then it is a continuation
of the previous line and the two combined represent the entire Device Marking for that device.

(6)
   Lead/Ball Finish - Orderable Devices may have multiple material finish options. Finish options are separated by a vertical ruled line. Lead/Ball Finish values may wrap to two lines if the finish
value exceeds the maximum column width.


Important Information and Disclaimer:The information provided on this page represents TI's knowledge and belief as of the date that it is provided. TI bases its knowledge and belief on information
provided by third parties, and makes no representation or warranty as to the accuracy of such information. Efforts are underway to better integrate information from third parties. TI has taken and
continues to take reasonable steps to provide representative and accurate information but may not have conducted destructive testing or chemical analysis on incoming materials and chemicals.
TI and TI suppliers consider certain information to be proprietary, and thus CAS numbers and other limited information may not be available for release.

In no event shall TI's liability arising out of such information exceed the total purchase price of the TI part(s) at issue in this document sold by TI to Customer on an annual basis.




                                                                                                Addendum-Page 3
```

## PDF 第 57 页

来源 [BQ76940.pdf PDF p.57](../../sources/BQ76940.pdf#page=57)

```text
                                                                   PACKAGE MATERIALS INFORMATION

www.ti.com                                                                                                               14-Nov-2014



TAPE AND REEL INFORMATION




*All dimensions are nominal
             Device           Package Package Pins   SPQ       Reel   Reel   A0            B0     K0     P1     W     Pin1
                               Type Drawing                  Diameter Width (mm)          (mm)   (mm)   (mm)   (mm) Quadrant
                                                               (mm) W1 (mm)
     BQ7692000PWR             TSSOP     PW     20    2000      330.0     16.4      6.95   7.1    1.6    8.0    16.0    Q1
     BQ7692003PWR             TSSOP     PW     20    2000      330.0     16.4      6.95   7.1    1.6    8.0    16.0    Q1
     BQ7692006PWR             TSSOP     PW     20    2000      330.0     16.4      6.95   7.1    1.6    8.0    16.0    Q1




                                                           Pack Materials-Page 1
```

## PDF 第 58 页

来源 [BQ76940.pdf PDF p.58](../../sources/BQ76940.pdf#page=58)

```text
                                                                PACKAGE MATERIALS INFORMATION

www.ti.com                                                                                                           14-Nov-2014




*All dimensions are nominal
             Device           Package Type   Package Drawing   Pins     SPQ     Length (mm)   Width (mm)   Height (mm)
     BQ7692000PWR               TSSOP              PW           20      2000       367.0        367.0         38.0
     BQ7692003PWR               TSSOP              PW           20      2000       367.0        367.0         38.0
     BQ7692006PWR               TSSOP              PW           20      2000       367.0        367.0         38.0




                                                        Pack Materials-Page 2
```

## PDF 第 59 页

来源 [BQ76940.pdf PDF p.59](../../sources/BQ76940.pdf#page=59)

```text

```

## PDF 第 60 页

来源 [BQ76940.pdf PDF p.60](../../sources/BQ76940.pdf#page=60)

```text

```

## PDF 第 61 页

来源 [BQ76940.pdf PDF p.61](../../sources/BQ76940.pdf#page=61)

```text

```

## PDF 第 62 页

来源 [BQ76940.pdf PDF p.62](../../sources/BQ76940.pdf#page=62)

```text

```

## PDF 第 63 页

来源 [BQ76940.pdf PDF p.63](../../sources/BQ76940.pdf#page=63)

```text
                                                              IMPORTANT NOTICE

Texas Instruments Incorporated and its subsidiaries (TI) reserve the right to make corrections, enhancements, improvements and other
changes to its semiconductor products and services per JESD46, latest issue, and to discontinue any product or service per JESD48, latest
issue. Buyers should obtain the latest relevant information before placing orders and should verify that such information is current and
complete. All semiconductor products (also referred to herein as “components”) are sold subject to TI’s terms and conditions of sale
supplied at the time of order acknowledgment.
TI warrants performance of its components to the specifications applicable at the time of sale, in accordance with the warranty in TI’s terms
and conditions of sale of semiconductor products. Testing and other quality control techniques are used to the extent TI deems necessary
to support this warranty. Except where mandated by applicable law, testing of all parameters of each component is not necessarily
performed.
TI assumes no liability for applications assistance or the design of Buyers’ products. Buyers are responsible for their products and
applications using TI components. To minimize the risks associated with Buyers’ products and applications, Buyers should provide
adequate design and operating safeguards.
TI does not warrant or represent that any license, either express or implied, is granted under any patent right, copyright, mask work right, or
other intellectual property right relating to any combination, machine, or process in which TI components or services are used. Information
published by TI regarding third-party products or services does not constitute a license to use such products or services or a warranty or
endorsement thereof. Use of such information may require a license from a third party under the patents or other intellectual property of the
third party, or a license from TI under the patents or other intellectual property of TI.
Reproduction of significant portions of TI information in TI data books or data sheets is permissible only if reproduction is without alteration
and is accompanied by all associated warranties, conditions, limitations, and notices. TI is not responsible or liable for such altered
documentation. Information of third parties may be subject to additional restrictions.
Resale of TI components or services with statements different from or beyond the parameters stated by TI for that component or service
voids all express and any implied warranties for the associated TI component or service and is an unfair and deceptive business practice.
TI is not responsible or liable for any such statements.
Buyer acknowledges and agrees that it is solely responsible for compliance with all legal, regulatory and safety-related requirements
concerning its products, and any use of TI components in its applications, notwithstanding any applications-related information or support
that may be provided by TI. Buyer represents and agrees that it has all the necessary expertise to create and implement safeguards which
anticipate dangerous consequences of failures, monitor failures and their consequences, lessen the likelihood of failures that might cause
harm and take appropriate remedial actions. Buyer will fully indemnify TI and its representatives against any damages arising out of the use
of any TI components in safety-critical applications.
In some cases, TI components may be promoted specifically to facilitate safety-related applications. With such components, TI’s goal is to
help enable customers to design and create their own end-product solutions that meet applicable functional safety standards and
requirements. Nonetheless, such components are subject to these terms.
No TI components are authorized for use in FDA Class III (or similar life-critical medical equipment) unless authorized officers of the parties
have executed a special agreement specifically governing such use.
Only those TI components which TI has specifically designated as military grade or “enhanced plastic” are designed and intended for use in
military/aerospace applications or environments. Buyer acknowledges and agrees that any military or aerospace use of TI components
which have not been so designated is solely at the Buyer's risk, and that Buyer is solely responsible for compliance with all legal and
regulatory requirements in connection with such use.
TI has specifically designated certain components as meeting ISO/TS16949 requirements, mainly for automotive use. In any case of use of
non-designated products, TI will not be responsible for any failure to meet ISO/TS16949.

Products                                                           Applications
Audio                                www.ti.com/audio              Automotive and Transportation      www.ti.com/automotive
Amplifiers                           amplifier.ti.com              Communications and Telecom         www.ti.com/communications
Data Converters                      dataconverter.ti.com          Computers and Peripherals          www.ti.com/computers
DLP® Products                        www.dlp.com                   Consumer Electronics               www.ti.com/consumer-apps
DSP                                  dsp.ti.com                    Energy and Lighting                www.ti.com/energy
Clocks and Timers                    www.ti.com/clocks             Industrial                         www.ti.com/industrial
Interface                            interface.ti.com              Medical                            www.ti.com/medical
Logic                                logic.ti.com                  Security                           www.ti.com/security
Power Mgmt                           power.ti.com                  Space, Avionics and Defense        www.ti.com/space-avionics-defense
Microcontrollers                     microcontroller.ti.com        Video and Imaging                  www.ti.com/video
RFID                                 www.ti-rfid.com
OMAP Applications Processors         www.ti.com/omap               TI E2E Community                   e2e.ti.com
Wireless Connectivity                www.ti.com/wirelessconnectivity


                             Mailing Address: Texas Instruments, Post Office Box 655303, Dallas, Texas 75265
                                            Copyright © 2016, Texas Instruments Incorporated
```
