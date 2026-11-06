#include "SPI.h"
#include "Adafruit_GFX.h"
#include "XPT2046_Touchscreen.h"
#include "Adafruit_ILI9341.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <SdFat.h>

//#define SPI_SRAM 1 //test1の処理を実行する。
#define SRAM_ERASE 0xFF

// SD <-> メモリ転送の対象 (S/W/B/L/V コマンドで使用)
#define DEV_SPI_SRAM 0
#define DEV_FUJISOKU 1

//SPI GPIO(MCP23S17 GPIOA, GPIOB) Definition
#define MCP23x17_OPE_WRITE 0x40
#define MCP23x17_OPE_WRITE_A2 0x48
#define MCP23x17_OPE_READ 0x41

#define MCP_ALL_INPUT 0xFF
#define MCP_ALL_OUTPUT 0x00

//register　address(BANK=1)    description                 datasheet
//PORT A
#define MCP23x17_DDR_A 0x00      //  Data Direction Register A     P18
#define MCP23x17_POL_A 0x01      //  Input Polarity A              P18
#define MCP23x17_GPINTEN_A 0x02  //  Interrupt enable              P19
#define MCP23x17_DEFVAL_A 0x03   //  Interrupt default             P19
#define MCP23x17_INTCON_A 0x04   //  Interrupt control register    P20
#define MCP23x17_IOCR 0x05       //  IO control register     P20
#define MCP23x17_GPUP_A 0x06     //  Pull Up Resistors A           P22
#define MCP23x17_INTF_A 0x07     //  Interrupt flag register       P22
#define MCP23x17_INTCAP_A 0x08   //  Interrupt capture register    P23
#define MCP23x17_GPIO_A 0x09     //  General Purpose IO A          P23
#define MCP23x17_OLAT_A 0x0A     //  NOT USED output latch         P24
//PORT B
#define MCP23x17_DDR_B 0x10      //  Data Direction Register B     P18
#define MCP23x17_POL_B 0x11      //  Input Polarity B              P18
#define MCP23x17_GPINTEN_B 0x12  //  Interrupt enable              P19
#define MCP23x17_DEFVAL_B 0x13   //  Interrupt default             P19
#define MCP23x17_INTCON_B 0x14   //  Interrupt control register    P20
#define MCP23x17_IOCR_B 0x15     //  IO control register     P20
#define MCP23x17_GPUP_B 0x16     //  Pull Up Resistors A           P22
#define MCP23x17_INTF_B 0x17     //  Interrupt flag register       P22
#define MCP23x17_INTCAP_B 0x18   //  Interrupt capture register    P23
#define MCP23x17_GPIO_B 0x19     //  General Purpose IO B          P23
#define MCP23x17_OLAT_B 0x1A     //  NOT USED output latch         P24
/*
//  address(BANK=0)     default  //  description                 datasheet
#define MCP23x17_DDR_A 0x00      //  Data Direction Register A     P18
#define MCP23x17_DDR_B 0x01      //  Data Direction Register B     P18
#define MCP23x17_POL_A 0x02      //  Input Polarity A              P18
#define MCP23x17_POL_B 0x03      //  Input Polarity B              P18
#define MCP23x17_GPINTEN_A 0x04  //  Interrupt enable              P19
#define MCP23x17_GPINTEN_B 0x05  //  Interrupt enable              P19
#define MCP23x17_DEFVAL_A 0x06   //  Interrupt default             P19
#define MCP23x17_DEFVAL_B 0x07   //  Interrupt default             P19
#define MCP23x17_INTCON_A 0x08   //  Interrupt control register    P20
#define MCP23x17_INTCON_B 0x09   //  Interrupt control register    P20
#define MCP23x17_IOCR 0x0A       //  IO control register           P20
#define MCP23x17_IOCR2 0x0B      //  NOT USED
#define MCP23x17_PUR_A 0x0C      //  Pull Up Resistors A           P22
#define MCP23x17_PUR_B 0x0D      //  Pull Up Resistors A           P22
#define MCP23x17_INTF_A 0x0E     //  Interrupt flag register       P22
#define MCP23x17_INTF_B 0x0F     //  Interrupt flag register       P22
#define MCP23x17_INTCAP_A 0x10   //  Interrupt capture register    P23
#define MCP23x17_INTCAP_B 0x11   //  Interrupt capture register    P23
#define MCP23x17_GPIO_A 0x12     //  General Purpose IO A          P23
#define MCP23x17_GPIO_B 0x13     //  General Purpose IO B          P23
#define MCP23x17_OLAT_A 0x14     //  NOT USED output latch         P24
#define MCP23x17_OLAT_B 0x15     //  NOT USED output latch         P24
*/
//  Initial IOCR(BANK=0)
#define MCP23x17_IOCR_BANK0 0x0A  //  IO control register     P20

//  IOCR = IO CONTROL REGISTER bit masks   - details datasheet P20
#define IOCR_BANK1 0x80   // BANK=1
#define IOCR_MIRROR 0x40  //  INT Pins Mirror bit.
#define IOCR_SEQOP 0x20   //  Sequential Operation mode bit.
#define IOCR_DISSLW 0x10  //  Slew Rate control bit for SDA output.
#define IOCR_HAEN 0x08    //  Hardware Address Enable bit (MCP23S17 only).
#define IOCR_ODR 0x04     //  Configures the INT pin as an open-drain output.
#define IOCR_INTPOL 0x02  //  This bit sets the polarity of the INT output pin.
#define IOCR_NI 0x01      //  Not implemented.

//SPI SRAM Definition
#define SRAM_OPE_READ 0x03
#define SRAM_OPE_WRITE 0x02
#define SRAM_OPE_MODE 0xFF
#define REG_READ 0x05
#define REG_WRITE 0x01
#define MODE_RW_BYTE 0x00
#define MODE_RW_PAGE 0x40  //32byte onetime
#define MODE_RW_SEQ 0x80

//SPI Chip Select pin number
#define CS_TFT 10
#define DC_TFT 9
#define CS_touch 8
#define CS_SDcard 7
#define CS_SRAM_512 6
#define CS_RESET 5
#define CS_MCP23S17 4
#define CS_FSRAM_512 1

#define IRQ_MCP 3
#define IRQ_touch 2

//MCP Ch (MCP23S17 GPIOA, GPIOB)
#define CH_MCP_BUS_ADDRESS 6
#define CH_MCP_BUS_DATA 7
#define CH_MCP_CONTROL 7

// all INPUT PULL UP
#define MCP7_PORT_PULUP 0b11111111
// GPIO SRAM control (B7-B0) A17 A16 nCS_SPI_SRAM nCS1_FUJISOKU nWE_SRAM nOE_62256 nOE_FUJISOKU nCS_62256(CS2_FUJISOKU)
//PE1接続時は/OE_62256 bit のみ OUTPUT 他はINPUTモードにする
// MCP23x17_IODIR 1:INPUT 0:OUTPUT
#define MCP_IODIR_PE1_CONNECT 0b11011011
#define MCP_IODIR_FUJISOKU_CONNECT 0b00000000
#define MCP_IODIR_SRAM_CONNECT 0b00000000
#define DISENABLE_SRAM 0b00111111
#define DISENABLE_FUJISOKU 0b00111110
#define AND_ENABLE_nWE 0b11110111
#define ENABLE_SRAM_nCS 0b00111110
#define AND_ENABLE_SRAM_nOE 0b11111011
#define ENABLE_FUJISOKU_nCS1_CS2 0b00101111
#define AND_ENABLE_FUJISOKU_nOE 0b11111101
#define AND_ENABLE_SPI_SRAM 0b11011111

//initialize　Adafruit_ILI9341　TFT
Adafruit_ILI9341 tft = Adafruit_ILI9341(CS_TFT, DC_TFT);
//initialize　XPT2046_Touchscreen
XPT2046_Touchscreen ts(CS_touch, IRQ_touch);  // Param 2 - Touch IRQ Pin - interrupt enablei

typedef struct BUTTON {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t t;
  char key;
} BUTTON;

BUTTON key_C = { 20, 20, 45, 45, 'C' };
BUTTON key_D = { 70, 20, 45, 45, 'D' };
BUTTON key_E = { 120, 20, 45, 45, 'E' };
BUTTON key_F = { 170, 20, 45, 45, 'F' };
BUTTON key_8 = { 20, 70, 45, 45, '8' };
BUTTON key_9 = { 70, 70, 45, 45, '9' };
BUTTON key_A = { 120, 70, 45, 45, 'A' };
BUTTON key_B = { 170, 70, 45, 45, 'B' };
BUTTON key_4 = { 20, 120, 45, 45, '4' };
BUTTON key_5 = { 70, 120, 45, 45, '5' };
BUTTON key_6 = { 120, 120, 45, 45, '6' };
BUTTON key_7 = { 170, 120, 45, 45, '7' };
BUTTON key_0 = { 20, 170, 45, 45, '0' };
BUTTON key_1 = { 70, 170, 45, 45, '1' };
BUTTON key_2 = { 120, 170, 45, 45, '2' };
BUTTON key_3 = { 170, 170, 45, 45, '3' };

BUTTON *key_array[16] = { &key_0, &key_1, &key_2, &key_3, &key_4, &key_5, &key_6, &key_7,
                          &key_8, &key_9, &key_A, &key_B, &key_C, &key_D, &key_E, &key_F };
float caliblation_x1, caliblation_y1, delta_x, delta_y;

void draw_button(BUTTON *button, uint16_t color);
boolean is_in_area(BUTTON *button, TS_Point point);
boolean is_touch_button(BUTTON *button);

void caliblation(void);
void drawCrossPoint(uint16_t x, uint16_t y, uint16_t color);
float convert_x(float x0);
float convert_y(float y0);

//for SD.h
// File myFile; 
//for SDfat.h
SdFat SD;
FsFile myFile; // SdFat V2では一般的にFsFileを使用（V1はSdFile）

void setup() {
  uint8_t dat;
  unsigned int ch;
  char strhex[10];

  //Serial initialize
  Serial.begin(115200);
     

  Serial.println("FUJISOKU Memory Card!");
  //SPI setup
  SPI.begin();
  //arduino DIGITAL Pin output for SPI CS
  pinMode(CS_TFT, OUTPUT);
  pinMode(CS_touch, OUTPUT);
  pinMode(CS_SDcard, OUTPUT);
  pinMode(CS_SRAM_512, OUTPUT);
  pinMode(CS_RESET, OUTPUT);
  pinMode(CS_MCP23S17, OUTPUT);
  pinMode(IRQ_touch, INPUT);
  pinMode(IRQ_MCP, INPUT);
  //arduino DIGITAL Pin for CS disenable
  digitalWrite(CS_TFT, HIGH);
  digitalWrite(CS_touch, HIGH);
  digitalWrite(CS_SDcard, HIGH);
  digitalWrite(CS_SRAM_512, HIGH);
  digitalWrite(CS_MCP23S17, HIGH);
  digitalWrite(CS_RESET, HIGH);
  // nRESET
  digitalWrite(CS_RESET, LOW);
  digitalWrite(CS_RESET, HIGH);
  //TFT setup
  tft.begin();
  tft.setRotation(0);
  //MCP23S17 BANK=1
  //アドレスA2:Hのときはレシスタ書き込みオペコード(0x40)のA2ビットを1にしないとIOCRが指定されない!!!
  //アドレス0-3(A2:0)のレシスタ書き込みオペコードは(0x40)だが
  //アドレス4-7(A2:1)のレシスタ書き込みオペコードは(0x48)となる
  //日本語版技術仕様書には書いてない！！英語版にも書いてない！！ひどすぎる！！
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE);
  SPI.transfer(MCP23x17_IOCR_BANK0);
  SPI.transfer(IOCR_BANK1 + IOCR_HAEN + IOCR_ODR);
  digitalWrite(CS_MCP23S17, HIGH);
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE_A2);
  SPI.transfer(MCP23x17_IOCR_BANK0);
  SPI.transfer(IOCR_BANK1 + IOCR_HAEN + IOCR_ODR);
  digitalWrite(CS_MCP23S17, HIGH);

  // ONCE after RESET
  for (ch = 0; ch < 8; ch++) {
    write_mcp_register(ch, MCP23x17_OLAT_A, 0x00);
    write_mcp_register(ch, MCP23x17_OLAT_B, 0x00);
    write_mcp_register(ch, MCP23x17_DDR_A, MCP_ALL_INPUT);
    write_mcp_register(ch, MCP23x17_DDR_B, MCP_ALL_INPUT);
    write_mcp_register(ch, MCP23x17_GPUP_A, 0b11111111);  //All pins PullUp
    write_mcp_register(ch, MCP23x17_GPUP_B, 0b11111111);
    //write_mcp_register(ch, MCP23x17_GPUP_A, 0b00000000);
    //write_mcp_register(ch, MCP23x17_GPUP_B, 0b00000000);
  }
  //SRAM BUS CONTROL setup
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_B, MCP_ALL_INPUT);
  //write_mcp_register(CH_MCP_CONTROL, MCP23x17_DDR_B, MCP_ALL_OUTPUT);      // CH8 PORT-B CONTROL+A16-A17 Output
  // CH7 MCP PortB control [A17][A16][CS_SPI_SRAM][NC][/WE_SRAM][/OE_62256][/OE_FUJISOKU][/CS_62256(CS_FUJISOKU)]
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_DDR_B, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_SRAM);  // All control disable
  // SDカードの初期化
  Serial.print("Initializing SD card...");
  if (!SD.begin(CS_SDcard)) {
    Serial.println("initialization failed!");
    SD.initErrorHalt();
    while (1);
  }
  Serial.println("initialization done.");
}

void loop() {
  char sfilename[16] = "fujisoku"; 
  unsigned int i;
  unsigned int long ii;
  unsigned int iii;
  uint8_t dat;
  char inkey;       // 入力文字格納用
 
  Serial.println("R:FUJISOKU SRAM -> SPI SRAM, C:SPI SRAM -> FUJISOKU SRAM");
  Serial.println("S:SPI SRAM -> SD , W:SD -> SPI SRAM"); 
  Serial.println("E:Erase SPI SRAM , e:Erase FUJISOKU SRAM");  
  Serial.println("X:Compare SPI SRAM <-> FUJISOKU SRAM , T:Test SPI SRAM");
  Serial.println("B:FUJISOKU SRAM -> SD , L:SD -> FUJISOKU SRAM , V:Verify SD <-> FUJISOKU SRAM");  
  // 読み込み情報の有無確認

  if (Serial.available() > 0) {
    inkey = Serial.read();
    switch (inkey) {
        case 'R':
            read_silver_memorycard();
            break;
        case 'C':
            copy_silver_memorycard();
            break;
        case 'X':
            compare_silver_memorycard();
            break;
        case 'S':
            save_sdcard_silver_memorycard(sfilename, DEV_SPI_SRAM);
            break;
        case 'W':
            write_sdcard_silver_memorycard(sfilename, DEV_SPI_SRAM);
            break;
        case 'B':
            save_sdcard_silver_memorycard(sfilename, DEV_FUJISOKU);
            break;
        case 'L':
            write_sdcard_silver_memorycard(sfilename, DEV_FUJISOKU);
            break;
        case 'V':
            verify_sdcard_silver_memorycard(sfilename, DEV_FUJISOKU);
            break;
        case 'T':
            test_spi_sram();
            break;
        default:
            break;
    }
  }
  //SRAM BUS CONTROL setup
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_B, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_DDR_B, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, 0b00111111);  // All control disable
  
  delay(1000);
}

/*
  char key='\0';
  if (ts.touched()) {
    for(uint8_t i=0; i<16; i++){
      if(is_touch_button(key_array[i])) key = key_array[i]->key;
    }
  }
  if(key != '\0'){
    Serial.print("Touch:");
    Serial.println(key);
  }
*/

/*
void SdFileList() {   //SDファイル一覧
   File dir = SD.open("/");
   while (1) {
       File entry =   dir.openNextFile();
       if (!entry) {
           break;
       }
       if (!entry.isDirectory()) {
           Serial.print('\t'); Serial.print(entry.name());
           Serial.print("\t"); Serial.println(entry.size(), DEC);
       }
       entry.close();
   }
}

String SdFileRead(String p_file_name) {   //SDファイル読み込み
   String str;
   File file = SD.open(p_file_name);
   Serial.print(F("SD FileRead: ")); Serial.print(p_file_name);
   if (file) {
       while (file.available()) {
          //file.seek(500); //これは位置を変えない
          str += char(file.read());
       }
       Serial.print(F(" > ")); Serial.println(str);
   } else {
       Serial.println(F(" error..."));
   }
   file.close();
   return str;
}

void SdFileWrite(String p_file_name, String p_str) {   //SDファイル書き込み
   File file = SD.open(p_file_name , FILE_WRITE | O_TRUNC);
   Serial.print(F("SD FileWrite: ")); Serial.print(p_file_name);
   if (file) {
       file.print(p_str); Serial.print(F(" << ")); Serial.println(p_str);
   } else {
       Serial.println(F(" error..."));
   }
   file.close();
}
*/

//Copy 
void read_silver_memorycard() {
  unsigned int i;
  unsigned int long ii;
  uint8_t dat;
  char address[7] = "";  // "R%04X:" = 6文字 + NUL
  char strhex[10];
  const size_t UNIT_SIZE = 0x10;

  Serial.println("Erase SRAM:");
  for (i = 0; i < 0x8000; i++) {
    #ifndef SPI_SRAM
      write_spi_sram_byte(i, SRAM_ERASE);
    #else
      write_sram(i, SRAM_ERASE);
    #endif
  }
  Serial.println("READ Silver Reed Memory Card:");
  for (ii = 0; ii < 0x8000; ii = ii + UNIT_SIZE) {
    snprintf(address, 7, "R%04X:", ii);
    Serial.print(address);
    for (i = 0; i < UNIT_SIZE; i++) {
      dat = read_fujisoku(i + ii);
      #ifndef SPI_SRAM  
        write_spi_sram_byte(i + ii, dat);
      #else 
        write_sram(i + ii, dat);
      #endif
      snprintf(strhex, 4, "%02X:", dat);
      Serial.print(strhex);
    }
    Serial.println("");
  }  
}

// 32kB
void copy_silver_memorycard() {
  unsigned int i;
  unsigned int long ii;
  uint8_t dat;
  char address[7] = "";
  char strhex[10];
  const size_t UNIT_SIZE = 0x10;

  Serial.println("Erase FUJISOKU SRAM:");
  for (i = 0; i < 0x8000; i++) {
    write_fujisoku(i, SRAM_ERASE);
  }

  Serial.println("Copy Silver Reed Memory Card:");
  for (ii = 0; ii < 0x8000; ii = ii + UNIT_SIZE) {
    snprintf(address, 7, "C%04X:", ii);
    Serial.print(address);
    for (i = 0; i < UNIT_SIZE; i++) {
      #ifndef SPI_SRAM
        dat = read_spi_sram_byte(i + ii);
      #else 
        dat = read_sram(i + ii);
      #endif
      write_fujisoku(i + ii, dat);
      snprintf(strhex, 4, "%02X:", dat);
      Serial.print(strhex);
    }
  Serial.println("");
  }
}

// 32kB
void compare_silver_memorycard() {
  unsigned int i;
  unsigned int long ii;
  uint8_t dat;
  char address[7] = "";
  const size_t UNIT_SIZE = 0x80;

  Serial.println("Compare Silver Reed Memory Card:");
  for (ii = 0; ii < 0x8000; ii = ii + UNIT_SIZE) {
    snprintf(address, 7, "X%04X:", ii);
    Serial.print(address);
    for (i = 0; i < UNIT_SIZE; i++) {
      #ifndef SPI_SRAM
        if(read_spi_sram_byte(i + ii) == read_fujisoku(i + ii)){
      #else
        if(read_sram(i + ii) == read_fujisoku(i + ii)){
      #endif
        Serial.print(".");
      }else{
        Serial.print("X");
      }
      //delay(1);
    }
    Serial.println("");
  }
}

//32K
// =====================================================================
// メモリ(dev: DEV_SPI_SRAM / DEV_FUJISOKU) -> SD カード への保存 (32KB)
// =====================================================================
void save_sdcard_silver_memorycard(const char *sp_filename, uint8_t dev) {
    const size_t BUF_SIZE = 0x400;   // 1024
    uint8_t buffer[BUF_SIZE];

    char sfilename[16];
    char strhex[8];
    char address[16];
    unsigned long ii;
    unsigned int  iii = 0;

    // ---- ファイル名の組み立て（先頭8文字 + ".bin"）----
    if (sp_filename != nullptr && sp_filename[0] != '\0') {
        snprintf(sfilename, sizeof(sfilename), "%.8s.bin", sp_filename);
    } else {
        strncpy(sfilename, "fujisoku.bin", sizeof(sfilename));
        sfilename[sizeof(sfilename) - 1] = '\0';
    }

    Serial.print("Save ");
    Serial.print(dev_name(dev));
    Serial.print(" -> SD card : ");
    Serial.println(sfilename);

    // ---- 既存ファイルの削除 ----
    if (SD.exists(sfilename)) {
        if (SD.remove(sfilename)) {
            Serial.println("ファイル削除成功");
        } else {
            Serial.println("ファイル削除失敗");
            return;
        }
    }

    // O_WRONLY | O_CREAT | O_TRUNC : 上書き保存
    if (!myFile.open(sfilename, O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.println("ファイルのオープンに失敗しました");
        return;
    }

    // ---- 本体: 0x0000 〜 0x7FFF を BUF_SIZE 単位で書き込み ----
    for (ii = 0; ii < 0x8000UL; ii += BUF_SIZE) {
        iii++;
        snprintf(address, sizeof(address), "%02uSDW%04lX:", iii, ii);
        Serial.print(address);

        for (unsigned int i = 0; i < BUF_SIZE; i++) {
            uint8_t dat = dev_read(dev, i + ii);
            buffer[i] = dat;
            snprintf(strhex, sizeof(strhex), "%02X:", dat);
            Serial.print(strhex);
        }
        Serial.println("");

        if (myFile.write(buffer, BUF_SIZE) != (int)BUF_SIZE) {
            Serial.println("書き込みエラー");
            myFile.close();
            return;
        }
    }

    myFile.close();
    Serial.println("SAVEが完了しました");
}


// =====================================================================
// SD カード -> メモリ(dev: DEV_SPI_SRAM / DEV_FUJISOKU) への書き戻し (32KB)
// =====================================================================
void write_sdcard_silver_memorycard(const char *sp_filename, uint8_t dev) {
    const size_t UNIT_SIZE = 0x100;  // 256
    char sfilename[16];
    char strhex[8];
    char address[16];
    unsigned long ii;

    // ---- ファイル名の組み立て ----
    if (sp_filename != nullptr && sp_filename[0] != '\0') {
        snprintf(sfilename, sizeof(sfilename), "%.8s.bin", sp_filename);
    } else {
        strncpy(sfilename, "fujisoku.bin", sizeof(sfilename));
        sfilename[sizeof(sfilename) - 1] = '\0';
    }

    Serial.print("Write SD DATA -> ");
    Serial.print(dev_name(dev));
    Serial.print(" : ");
    Serial.println(sfilename);

    // ---- 読み込み専用でオープン ----
    if (!SD.exists(sfilename)) {
        Serial.println("ファイルが存在しません");
        return;
    }
    if (!myFile.open(sfilename, O_RDONLY)) {
        Serial.println("ファイルのオープンに失敗しました");
        return;
    }
    myFile.seekSet(0x00);

    // ---- 本体: 0x0000 〜 0x7FFF を UNIT_SIZE 単位で書き込み ----
    for (ii = 0; ii < 0x8000UL; ii += UNIT_SIZE) {
        snprintf(address, sizeof(address), "SDC%04lX:", ii);
        Serial.print(address);

        for (unsigned int i = 0; i < UNIT_SIZE; i++) {
            int v = myFile.read();   // EOF の場合 -1
            if (v < 0) {
                Serial.println("");
                Serial.println("予期しない EOF");
                myFile.close();
                return;
            }
            uint8_t dat = (uint8_t)v;
            dev_write(dev, i + ii, dat);
            snprintf(strhex, sizeof(strhex), "%02X:", dat);
            Serial.print(strhex);
        }
        Serial.println("");
    }

    myFile.close();
    Serial.println("コピーが完了しました");
}

// =====================================================================
// 対象メモリ(dev)の 1 バイト読み書き / 表示名
//   DEV_SPI_SRAM : SPI SRAM (SPI_SRAM 定義時は並列 62256)
//   DEV_FUJISOKU : FUJISOKU メモリカード
// =====================================================================
uint8_t dev_read(uint8_t dev, unsigned long address) {
  if (dev == DEV_FUJISOKU) return read_fujisoku(address);
#ifndef SPI_SRAM
  return read_spi_sram_byte((unsigned int)address);
#else
  return read_sram(address);
#endif
}

void dev_write(uint8_t dev, unsigned long address, uint8_t dat) {
  if (dev == DEV_FUJISOKU) {
    write_fujisoku(address, dat);
    return;
  }
#ifndef SPI_SRAM
  write_spi_sram_byte((unsigned int)address, dat);
#else
  write_sram(address, dat);
#endif
}

const char *dev_name(uint8_t dev) {
  return (dev == DEV_FUJISOKU) ? "FUJISOKU SRAM" : "SPI SRAM";
}

// =====================================================================
// SD ファイル と メモリ(dev) の全 32KB (0x0000-0x7FFF) 照合
//   '.' = 一致, 'X' = 不一致。最後に不一致数と先頭 8 件を表示する。
// =====================================================================
void verify_sdcard_silver_memorycard(const char *sp_filename, uint8_t dev) {
    const size_t UNIT_SIZE = 0x80;   // 128
    uint8_t buffer[UNIT_SIZE];
    char sfilename[16];
    char address[16];
    char line[48];
    unsigned long ii;
    unsigned long mismatch = 0;
    unsigned long bad_addr[8];
    uint8_t bad_sd[8];
    uint8_t bad_mem[8];

    // ---- ファイル名の組み立て ----
    if (sp_filename != nullptr && sp_filename[0] != '\0') {
        snprintf(sfilename, sizeof(sfilename), "%.8s.bin", sp_filename);
    } else {
        strncpy(sfilename, "fujisoku.bin", sizeof(sfilename));
        sfilename[sizeof(sfilename) - 1] = '\0';
    }

    Serial.print("Verify SD <-> ");
    Serial.print(dev_name(dev));
    Serial.print(" : ");
    Serial.println(sfilename);

    if (!SD.exists(sfilename)) {
        Serial.println("ファイルが存在しません");
        return;
    }
    if (!myFile.open(sfilename, O_RDONLY)) {
        Serial.println("ファイルのオープンに失敗しました");
        return;
    }
    myFile.seekSet(0x00);

    for (ii = 0; ii < 0x8000UL; ii += UNIT_SIZE) {
        if (myFile.read(buffer, UNIT_SIZE) != (int)UNIT_SIZE) {
            Serial.println("");
            Serial.println("NG: ファイルが 32KB (0x8000) に満たない");
            myFile.close();
            return;
        }
        snprintf(address, sizeof(address), "V%04lX:", ii);
        Serial.print(address);

        for (unsigned int i = 0; i < UNIT_SIZE; i++) {
            uint8_t mem = dev_read(dev, ii + i);
            if (mem == buffer[i]) {
                Serial.print(".");
            } else {
                Serial.print("X");
                if (mismatch < 8) {
                    bad_addr[mismatch] = ii + i;
                    bad_sd[mismatch] = buffer[i];
                    bad_mem[mismatch] = mem;
                }
                mismatch++;
            }
        }
        Serial.println("");
    }

    // 32KB を超えるデータがファイルに残っていないか
    bool longer = (myFile.read() >= 0);
    myFile.close();

    for (unsigned int k = 0; k < 8 && k < mismatch; k++) {
        snprintf(line, sizeof(line), "  %04lX: SD=%02X MEM=%02X", bad_addr[k], bad_sd[k], bad_mem[k]);
        Serial.println(line);
    }

    if (mismatch == 0 && !longer) {
        Serial.println("VERIFY OK: 32768 bytes all match");
    } else {
        if (mismatch > 0) {
            Serial.print("VERIFY NG: mismatch = ");
            Serial.print((unsigned int)mismatch);
            Serial.println(" / 32768 bytes");
        }
        if (longer) {
            Serial.println("NG: ファイルが 32KB より大きい");
        }
    }
}

//Hardware Control
void read_IOCR(){
  unsigned int  ch = 0;
  char strhex[256];
  Serial.print("read_mcp_register(IOCR):");
  for (ch = 0; ch < 8; ch++) {
    snprintf(strhex, 4, "%02X:", read_mcp_register(ch, MCP23x17_IOCR));
    Serial.print(strhex);
  }
  Serial.println("");
}
 
//READ GPIO register
uint8_t read_mcp_register(uint8_t ch, uint8_t address) {
  uint8_t dat;
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_READ + ch * 2);
  SPI.transfer(address);
  dat = SPI.transfer(0);
  digitalWrite(CS_MCP23S17, HIGH);
  return (dat);
}

//GPIO register write
void write_mcp_register(uint8_t ch, uint8_t address, uint8_t dat) {
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE + ch * 2);
  SPI.transfer(address);
  SPI.transfer(dat);
  digitalWrite(CS_MCP23S17, HIGH);
}

//SET SRAM Address set to MCP
void set_mcp_sram_address(unsigned long int address, uint8_t CS) {
  uint8_t addr1, addr2, addr3;

  addr1 = address & 0xff;
  addr2 = (address >> 8) & 0xff;
  addr3 = (address >> (8 + 2)) & 0b11000000;  //A17,A16
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_A, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_B, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_GPIO_A, addr1);  //A0-A7
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_GPIO_B, addr2);  //A8-A15
  // CH7 GPIOB control [A17][A16][CS_SPI_SRAM][NC][/WE_SRAM][/OE_62256][/OE_FUJISOKU][/CS_62256(CS_FUJISOKU)]
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, CS + addr3);  // A17 A16 Chip select(nCS_62256 CS_FUJISOKU)
}

// Read SRAM Memory  62256(SRAM 32kB)
uint8_t read_sram(unsigned long int address) {
  uint8_t dat;
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);  // Data bus direction change to input
  set_mcp_sram_address(address, ENABLE_SRAM_nCS);                      // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SRAM_nOE));  // nOE Output enable
  dat = read_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A);                                // Read data
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                          // nOE Output disenable ; A17,A16
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_SRAM);                      // All control pin disable
  return (dat);
}
// Read FUJISOKU SRAM Memory card (MAX 512kB)
uint8_t read_fujisoku(unsigned long int address) {
  uint8_t dat;
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);  // Data bus direction change to input
  set_mcp_sram_address(address, ENABLE_FUJISOKU_nCS1_CS2);             // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_FUJISOKU_nOE));  // nOE Output enable
  dat = read_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A);                                    // Read dat
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                              // nOE Output disenable ; A11,A12
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_FUJISOKU);                      // All control pin disable
  return (dat);
}
// Write SRAM Memory  62256(SRAM 32kB)
void write_sram(unsigned long int address, uint8_t dat) {
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_OUTPUT);  //DATA BUS OUTPUT
  set_mcp_sram_address(address, ENABLE_SRAM_nCS);                       // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_nWE));  // write enable + A11,A12
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A, dat);                           // Data set
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                     // write disenable
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_SRAM);                 // All control pin disable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);                  // Data bus direction change to input(Pull Up)
}
// Write FUJISOKU SRAM Memory card (MAX 512kB)
void write_fujisoku(unsigned long int address, uint8_t dat) {
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_OUTPUT);  //DATA BUS OUTPUT
  set_mcp_sram_address(address, ENABLE_FUJISOKU_nCS1_CS2);              // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_nWE));  // write enable + A11,A12
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A, dat);                           // Data set
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                     // write disenable
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_FUJISOKU);             // All control pin disable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);
}

//READ SPI SRAM register
uint8_t read_spi_sram_byte(unsigned int address) {
  uint8_t dat;
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  dat = SPI.transfer(MODE_RW_BYTE);
  digitalWrite(CS_SRAM_512, HIGH);

  return (dat);
}

//READ SPI SRAM on MCP register
uint8_t read_spi_sram4MCP_byte(unsigned int address) {
  uint8_t dat;
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  dat = SPI.transfer(MODE_RW_BYTE);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs ); 

  return (dat);
}
//WRITE SPI SRAM register
void write_spi_sram_byte(unsigned int address, uint8_t dat) {
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  SPI.transfer(dat);
  digitalWrite(CS_SRAM_512, HIGH);
}
//WRITE SPI SRAM on MCP register
void write_spi_sram4MCP_byte(unsigned int address, uint8_t dat) {
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  SPI.transfer(dat);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs ); 
}

// ---- SPI SRAM 自己診断 (T コマンド) -------------------------------------
// path 0: /CS = Arduino D6 (CS_SRAM_512), path 1: /CS = MCP23S17 GPB5 (nCS_SPI_SRAM)
uint8_t spi_sram_rd(uint8_t path, unsigned int a) {
  return path ? read_spi_sram4MCP_byte(a) : read_spi_sram_byte(a);
}

void spi_sram_wr(uint8_t path, unsigned int a, uint8_t d) {
  if (path) write_spi_sram4MCP_byte(a, d);
  else write_spi_sram_byte(a, d);
}

// モードレジスタ読み出し (RDMR 0x05)。応答があれば電源投入直後は 0x40(シーケンシャル)。
// 0xFF = MISO がHのまま(チップ無応答 / /CS 未接続)、0x00 = MISO が L 固定 or バイトモード
uint8_t spi_sram_rdmr(uint8_t path) {
  uint8_t dat;
  uint8_t address_cs = 0;
  if (path) {
    address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
    write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM));
  } else {
    digitalWrite(CS_SRAM_512, LOW);
  }
  SPI.transfer(REG_READ);
  dat = SPI.transfer(0);
  if (path) write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);
  else digitalWrite(CS_SRAM_512, HIGH);
  return dat;
}

// 0x0000-0x000F と 0x7FF0-0x7FFF を書き込み→読み戻しして /CS 経路ごとに判定。元の内容は復元する。
void test_spi_sram() {
  const unsigned int base[2] = { 0x0000, 0x7FF0 };
  char strhex[8];

  for (uint8_t path = 0; path < 2; path++) {
    uint8_t keep[32];
    unsigned int bad = 0;

    Serial.print(path ? "SPI SRAM /CS=MCP GPB5: RDMR=" : "SPI SRAM /CS=D6      : RDMR=");
    snprintf(strhex, sizeof(strhex), "%02X", spi_sram_rdmr(path));
    Serial.println(strhex);

    for (uint8_t k = 0; k < 2; k++)
      for (uint8_t i = 0; i < 16; i++) keep[k * 16 + i] = spi_sram_rd(path, base[k] + i);

    Serial.print("  readback:");
    for (uint8_t pass = 0; pass < 2; pass++) {
      for (uint8_t k = 0; k < 2; k++) {
        for (uint8_t i = 0; i < 16; i++) {
          uint8_t pat = (uint8_t)(0xA0 + i);
          if (pass) pat = (uint8_t)~pat;
          spi_sram_wr(path, base[k] + i, pat);
        }
        for (uint8_t i = 0; i < 16; i++) {
          uint8_t pat = (uint8_t)(0xA0 + i);
          if (pass) pat = (uint8_t)~pat;
          uint8_t r = spi_sram_rd(path, base[k] + i);
          if (r != pat) bad++;
          if (pass == 0 && k == 0) {
            snprintf(strhex, sizeof(strhex), "%02X:", r);
            Serial.print(strhex);
          }
        }
      }
    }
    Serial.println("");

    for (uint8_t k = 0; k < 2; k++)
      for (uint8_t i = 0; i < 16; i++) spi_sram_wr(path, base[k] + i, keep[k * 16 + i]);

    Serial.print(bad == 0 ? "  PASS" : "  FAIL mismatches=");
    if (bad) Serial.print(bad);
    Serial.println(bad ? "/64" : "");
  }
}

//READ SPI SRAM sequential
void read_spi_sram_seq(unsigned int address, uint8_t *dat, unsigned int bytes) {
  unsigned int i;
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  for (i = 0; i < bytes; i++)
    *(dat++) = SPI.transfer(MODE_RW_BYTE);
  digitalWrite(CS_SRAM_512, HIGH);
}

//READ SPI SRAM on MCP sequential
void read_spi_sram4MCP_seq(unsigned int address, uint8_t *dat, unsigned int bytes) {
  unsigned int i;
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  for (i = 0; i < bytes; i++)
    *(dat++) = SPI.transfer(MODE_RW_BYTE);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs); 
}

//WRITE SPI SRAM sequential
void write_spi_sram_seq(unsigned int address, uint8_t *dat, unsigned int bytes) {
  unsigned int i;
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  for (i = 0; i < bytes; i++)
    SPI.transfer(*(dat++));
  digitalWrite(CS_SRAM_512, HIGH);
}

//WRITE SPI SRAM on MCP sequential
void write_spi_sram4MCP_seq(unsigned int address, uint8_t *dat, unsigned int bytes) {
  unsigned int i;
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  for (i = 0; i < bytes; i++)
    SPI.transfer(*(dat++));
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs); 
}

//SET SPI SRAM READ MODE
void set_spi_sram_read_mode(uint8_t reg) {
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(REG_READ);
  SPI.transfer(reg);
  digitalWrite(CS_SRAM_512, HIGH);
}

//SET SPI SRAM on MCP READ MODE
void set_spi_sram4MCP_read_mode(uint8_t reg) {
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(REG_READ);
  SPI.transfer(reg);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs); 
}

//SET SPI SRAM WRITE MODE
void set_spi_sram_write_mode(uint8_t reg) {
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(REG_WRITE);
  SPI.transfer(reg);
  digitalWrite(CS_SRAM_512, HIGH);
}

//SET SPI SRAM on MCP WRITE MODE
void set_spi_sram4MCP_write_mode(uint8_t reg) {
 uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM)); 
  SPI.transfer(REG_WRITE);
  SPI.transfer(reg);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs); 
}

//ボタンを表示する
void draw_button(BUTTON *button, uint16_t color) {
  tft.drawRect(button->x, button->y, button->w, button->t, color);
  tft.setCursor(button->x + 15, button->y + 12);
  tft.setTextColor(color);
  tft.setTextSize(3);
  char s[3] = { button->key, '\0' };
  tft.print(s);
}

// タッチパネルで読み出した座標が、設定したボタンか確認する
boolean is_in_area(BUTTON *button, TS_Point point) {
  return ((convert_x(point.x) > button->x) && (convert_x(point.x) < (button->x + button->w)) && (convert_y(point.y) > button->y) && (convert_y(point.y) < (button->y + button->t)));
}

//ボタンが押されたか確認し押されたのならばトラッキングを行う
boolean is_touch_button(BUTTON *button) {
  TS_Point point = ts.getPoint();
  boolean is_touch = false;
  if (is_in_area(button, point)) {
    while (ts.touched()) {
      point = ts.getPoint();
      if (is_in_area(button, point)) {
        draw_button(button, ILI9341_RED);
        is_touch = true;
      } else {
        draw_button(button, ILI9341_WHITE);
        is_touch = false;
      }
    }
    draw_button(button, ILI9341_WHITE);
  }
  return (is_touch);
}

//タッチパネルのキャリブレーション
void caliblation(void) {
  drawCrossPoint(20, 20, ILI9341_RED);
  drawCrossPoint(220, 300, ILI9341_WHITE);
  uint8_t count = 0;
  float sum_x = 0.0;
  float sum_y = 0.0;
  while (true) {
    while (ts.touched() && count < 10) {
      TS_Point point = ts.getPoint();
      sum_x += point.x;
      sum_y += point.y;
      count++;
    }
    if (count == 10) break;
  }

  caliblation_x1 = sum_x / 10.0;
  caliblation_y1 = sum_y / 10.0;

  drawCrossPoint(20, 20, ILI9341_WHITE);
  drawCrossPoint(220, 300, ILI9341_RED);

  count = 0;
  sum_x = 0.0;
  sum_y = 0.0;
  while (true) {
    while (ts.touched() && count < 10) {
      TS_Point point = ts.getPoint();
      if ((point.x > (caliblation_x1 - 500)) && (point.x < (caliblation_x1 + 500)) && (point.y > (caliblation_y1 - 500)) && (point.y < (caliblation_y1 + 500))) break;
      sum_x += point.x;
      sum_y += point.y;
      count++;
    }
    if (count == 10) break;
  }
  float caliblation_x2 = sum_x / 10.0;
  float caliblation_y2 = sum_y / 10.0;
  delta_x = 200.0 / (caliblation_x2 - caliblation_x1);
  delta_y = 280.0 / (caliblation_y2 - caliblation_y1);

  drawCrossPoint(20, 20, ILI9341_BLACK);
  drawCrossPoint(220, 300, ILI9341_BLACK);

  char s[64];
  Serial.print("Cal(");
  Serial.print(caliblation_x1);
  Serial.print(",");
  Serial.print(caliblation_y1);
  Serial.println(")");
  Serial.print("Delta(");
  Serial.print(delta_x * 1000.0);
  Serial.print(",");
  Serial.print(delta_y * 1000.0);
  Serial.println(") at x1000");
}

//*調整用の十字を表示する
void drawCrossPoint(uint16_t x, uint16_t y, uint16_t color) {
  tft.drawLine(x - 5, y, x + 5, y, color);
  tft.drawLine(x, y - 5, x, y + 5, color);
  tft.drawCircle(x, y, 2, color);
}

//タッチパネルで読み取ったX座標をグラフィック座標に補正する
float convert_x(float x0) {
  return (20.0 + (float)(x0 - caliblation_x1) * delta_x);
}

//タッチパネルで読み取ったY座標をグラフィック座標に補正する
float convert_y(float y0) {
  return (20.0 + (float)(y0 - caliblation_y1) * delta_y);
}

/*
//タッチパネルで読み出した座標が、設定したボタンか確認する
boolean is_in_area(BUTTON button, TS_Point point) {
  return((convert_x(point.x) > button->x) && (convert_x(point.x) < (button->x + button->w)) && (convert_y(point.y) > button->y) && (convert_y(point.y) < (button->y + button->t)));
}

//ボタンが押されたか確認し押されたのならばトラッキングを行う
boolean is_touch_button(BUTTON *button){
  TS_Point point = ts.getPoint();
  boolean is_touch = false;
  if(is_in_area(button, point)) {
    while(ts.touched()){
      point = ts.getPoint();
      if(is_in_area(button, point)){
        draw_button(button, ILI9341_RED);
        is_touch = true;
      }
      else{
          draw_button(button, ILI9341_WHITE);
          is_touch = false;
      }
    }
  draw_button(button, ILI9341_WHITE);
  }
  return(is_touch);
}

unsigned long testText() {
  tft.fillScreen(ILI9341_BLACK);
  unsigned long start = micros();
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.println("Hello World!");
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.println(1234.56);
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(3);
  tft.println(0xDEADBEEF, HEX);
  tft.println();
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(5);
  tft.println("Groop");
  ft.setTextSize(2);
  tft.println("I implore thee,");
  tft.setTextSize(1);
  tft.println("my foonting turlingdromes.");
  tft.println("And hooptiously drangle me");
  tft.println("with crinkly bindlewurdles,");
  tft.println("Or I will rend thee");
  tft.println("in the gobberwarts");
  tft.println("with my blurglecruncheon,");
  tft.println("see if I don't!");
      
  return micros() - start;
}
*/

//DISPLAY TFT
void print_tft(char str_print[]) {

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  //tft.setScrollArea(0, 320, 0);
  tft.scrollTo(0);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setRotation(0);
  tft.println(str_print);
}

void print_tft2() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.println("my foonting turlingdromes.");
  tft.println("And hooptiously drangle me");
  tft.println("with crinkly bindlewurdles,");
  tft.println("Or I will rend thee");
  tft.println("in the gobberwarts");
  tft.println("with my blurglecruncheon,");
  tft.println("see if I don't!");
}

void hexdump(const void *data, size_t size) {
  char ascii[17];
  size_t i, j;
  ascii[16] = '\0';
  for (i = 0; i < size; ++i) {
    printf("%02X ", ((unsigned char *)data)[i]);
    if (((unsigned char *)data)[i] >= ' ' && ((unsigned char *)data)[i] <= '~') {
      ascii[i % 16] = ((unsigned char *)data)[i];
    } else {
      ascii[i % 16] = '.';
    }
    if ((i + 1) % 8 == 0 || i + 1 == size) {
      printf(" ");
      if ((i + 1) % 16 == 0) {
        printf("|  %s \n", ascii);
      } else if (i + 1 == size) {
        ascii[(i + 1) % 16] = '\0';
        if ((i + 1) % 16 <= 8) {
          printf(" ");
        }
        for (j = (i + 1) % 16; j < 16; ++j) {
          printf("   ");
        }
        printf("|  %s \n", ascii);
      }
    }
  }
}


/*
int main()
{
	char *s = "To find information use the search box on the top right corner of the screen, or categorically browse the wiki using the Documentation topic links provided below.";
	ft_print_memory(s);
	return 0;

/*
int main()
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {  // ファイルオープン失敗時
        abort();
    }
    size_t wanted_len = 256;
    char buf[256];
    ssize_t len = read(fd, buf, wanted_len);
    if (len != wanted_len) {  // バリデーションチェック
        abort();
    }
    close(fd);
    hexdump(buf, wanted_len);  // 16進数ダンプ
}
*/