// MIT License
// 
// Copyright (c) 2023 Travis Smith
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
// and associated documentation files (the "Software"), to deal in the Software without 
// restriction, including without limitation the rights to use, copy, modify, merge, publish, 
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom 
// the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or 
// substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "DriveDirLoad.h"

//these functions triggered from ISR and use current menu selection information while c64 code waits

void HandleExecution()
{
   StructMenuItem MenuSel = MenuSource[SelItemFullIdx]; //Condensed pointer to selected menu item
   
   if (MenuSel.ItemType == rtNone || MenuSel.ItemType == rtUnknown) return;  //no action taken for these types
   
   
   //if SD card or USB Drive,  update path & load dir   or   load file to RAM
   if (IO1[rWRegCurrMenuWAIT] == rmtSD || IO1[rWRegCurrMenuWAIT] == rmtUSBDrive) 
   {
      bool SD_nUSBDrive = (IO1[rWRegCurrMenuWAIT] == rmtSD);
      
      if (MenuSel.ItemType == rtFileHex)  //FW update from hex file
      {
         char FullFilePath[MaxPathLength];
         
         if (strlen(DriveDirPath) == 1 && DriveDirPath[0] == '/') sprintf(FullFilePath, "/%s", MenuSel.Name);  // at root
         else sprintf(FullFilePath, "%s/%s", DriveDirPath, MenuSel.Name);

         DoFlashUpdate(SD_nUSBDrive, FullFilePath);
         IO1[rwRegFWUpdStatCont] = rFWUSCC64Finish; //last thing, tell C64 we're done
         return;  //we're done here...
      }
      
      if (MenuSel.ItemType == rtDirectory)
      {  //edit path as needed and load the new directory from SD/USB
         
         if(strcmp(MenuSel.Name, UpDirString)==0)
         {  //up dir
            char * LastSlash = strrchr(DriveDirPath, '/'); //find last slash
            if (LastSlash != NULL) LastSlash[0] = 0;  //terminate it there 
         }
         else strcat(DriveDirPath, MenuSel.Name); //append selected dir name
         
         LoadDirectory(SD_nUSBDrive); 
         return;  //we're done here...
      }
      
      if(!LoadFile(&MenuSel, SD_nUSBDrive)) MenuSel.ItemType=rtUnknown; //mark unknown if error      

      MenuSel.Code_Image = RAM_Image;
   }
    
   if (IO1[rWRegCurrMenuWAIT] == rmtUSBHost)
   {
      MenuSel.Code_Image = HOST_Image; 
   }
   
   if (MenuSel.ItemType == rtFileCrt) ParseCRTFile(&MenuSel); //will update MenuSel.ItemType & .Code_Image, if checks ok
 
   if (MenuSel.ItemType == rtFileP00) ParseP00File(&MenuSel); //will update MenuSel.ItemType & .Code_Image, if checks ok

   //has to be distilled down to one of these by this point, only ones supported so far.
   //Emulate ROM or prep PRG tranfer
   uint8_t CartLoaded = false;
   switch(MenuSel.ItemType)
   {
      case rtBin16k:
         SetGameAssert;
         SetExROMAssert;
         LOROM_Image = MenuSel.Code_Image;
         HIROM_Image = MenuSel.Code_Image+0x2000;
         CartLoaded=true;
         break;
      case rtBin8kHi:
         SetGameAssert;
         SetExROMDeassert;
         LOROM_Image = NULL;
         HIROM_Image = MenuSel.Code_Image;
         CartLoaded=true;
         NVIC_DISABLE_IRQ(IRQ_ENET); //disable ethernet interrupt when emulating VIC cycles
         NVIC_DISABLE_IRQ(IRQ_PIT);
         EmulateVicCycles = true;
         break;
      case rtBin8kLo:
         SetGameDeassert;
         SetExROMAssert;
         LOROM_Image = MenuSel.Code_Image;
         HIROM_Image = NULL;
         CartLoaded=true;
         break;
      case rtBinC128:
         SetGameDeassert;
         SetExROMDeassert;
         LOROM_Image = MenuSel.Code_Image;
         HIROM_Image = NULL;
         CartLoaded=true;
         break;      
      case rtFilePrg:
         //set up for transfer
         MenuSource[SelItemFullIdx].Code_Image = MenuSel.Code_Image; 
         MenuSource[SelItemFullIdx].Size = MenuSel.Size; //only copy the pointer & Size back, not type
         IO1[rRegStrAddrLo]=MenuSel.Code_Image[0];
         IO1[rRegStrAddrHi]=MenuSel.Code_Image[1];
         IO1[rRegStrAvailable]=0xff;
         StreamOffsetAddr = 2; //set to start of data
         break;
   }
   
   if (CartLoaded)
   {
      doReset=true;
      IOHandlerInitToNext();
   }

}

void MenuChange()
{
   switch(IO1[rWRegCurrMenuWAIT])
   {
      case rmtTeensy:
         MenuSource = TeensyROMMenu; 
         SetNumItems(sizeof(TeensyROMMenu)/sizeof(TeensyROMMenu[0]));
         break;
      case rmtSD:
         stpcpy(DriveDirPath, "/");
         LoadDirectory(true);
         MenuSource = DriveDirMenu; 
         break;
      case rmtUSBDrive:
         stpcpy(DriveDirPath, "/");
         LoadDirectory(false);
         MenuSource = DriveDirMenu; 
         break;
      case rmtUSBHost:
         MenuSource = &USBHostMenu; 
         SetNumItems(NumUSBHostItems);
         break;
   }
}

bool LoadFile(StructMenuItem* MyMenuItem, bool SD_nUSBDrive) 
{
   char FullFilePath[MaxPathLength];

   if (strlen(DriveDirPath) == 1 && DriveDirPath[0] == '/') sprintf(FullFilePath, "%s%s", DriveDirPath, MenuSource[SelItemFullIdx].Name);  // at root
   else sprintf(FullFilePath, "%s/%s", DriveDirPath, MenuSource[SelItemFullIdx].Name);
      
   Serial.printf("Openning: %s\n", FullFilePath);
   
   File myFile;
   if (SD_nUSBDrive) myFile= SD.open(FullFilePath, FILE_READ);
   else myFile= firstPartition.open(FullFilePath, FILE_READ);
   
   if (!myFile) return false;

   free(RAM_Image);
   RAM_Image = (uint8_t*)malloc(myFile.size());
   Serial.printf("Size: %ul bytes\n", myFile.size());

   uint16_t count=0;
   while (myFile.available() && count < myFile.size()) RAM_Image[count++]=myFile.read();

   if (count != myFile.size()) return false;
   MyMenuItem->Size = count;
   myFile.close();
   return true;
}

void LoadDirectory(bool SD_nUSBDrive) 
{
   uint16_t NumItems = 0;
   File dir;
      
   if (SD_nUSBDrive) dir = SD.open(DriveDirPath);//SD card
   else dir = firstPartition.open(DriveDirPath); //USB Drive
   
   if (!(strlen(DriveDirPath) == 1 && DriveDirPath[0] == '/'))
   {  // *not* at root, add up dir option
      NumItems = 1;
      strcpy(DriveDirMenu[0].Name, UpDirString);
      DriveDirMenu[0].ItemType = rtDirectory;
   }
   
   const char *filename;
   
   while (File entry = dir.openNextFile()) 
   {
      filename = entry.name();
      if (entry.isDirectory())
      {
         DriveDirMenu[NumItems].Name[0] = '/';
         memcpy(DriveDirMenu[NumItems].Name+1, filename, MaxItemNameLength-1);
      }
      else memcpy(DriveDirMenu[NumItems].Name, filename, MaxItemNameLength);
      
      DriveDirMenu[NumItems].Name[MaxItemNameLength-1]=0; //terminate in case too long. 
      
      if (entry.isDirectory()) DriveDirMenu[NumItems].ItemType = rtDirectory;
      else 
      {
         char* Extension = (filename + strlen(filename) - 4);
         for(uint8_t cnt=1; cnt<=3; cnt++) if(Extension[cnt]>='A' && Extension[cnt]<='Z') Extension[cnt]+=32;
         
         if (strcmp(Extension, ".prg")==0) DriveDirMenu[NumItems].ItemType = rtFilePrg;
         else if (strcmp(Extension, ".crt")==0) DriveDirMenu[NumItems].ItemType = rtFileCrt;
         else if (strcmp(Extension, ".hex")==0) DriveDirMenu[NumItems].ItemType = rtFileHex;
         else if (strcmp(Extension, ".p00")==0) DriveDirMenu[NumItems].ItemType = rtFileP00;
         else DriveDirMenu[NumItems].ItemType = rtUnknown;
      }
      
      //Serial.printf("%d- %s\n", NumItems, DriveDirMenu[NumItems].Name); 
      entry.close();
      if (NumItems++ == MaxMenuItems)
      {
         Serial.println("Too many files!");
         break;
      }
   }
   
   SetNumItems(NumItems);
}

void ParseP00File(StructMenuItem* MyMenuItem)   
{  //update .ItemType(rtUnknown or rtFilePrg) & .Code_Image
   //Sources:
   // https://www.infinite-loop.at/Power64/Documentation/Power64-ReadMe/AE-File_Formats.html
   
   if(strcmp((char*)MyMenuItem->Code_Image, "C64File") == 0)
   {
      MyMenuItem->Code_Image += 26;
      MyMenuItem->ItemType = rtFilePrg;
   }
   else
   {
      Serial.println("Magic constant not found");
      MyMenuItem->ItemType = rtUnknown;
   }
}

void ParseCRTFile(StructMenuItem* MyMenuItem)   
{  //update .ItemType(rtUnknown or rtBin*) & .Code_Image
   //Sources:
   // https://codebase64.org/doku.php?id=base:crt_file_format
   // https://rr.pokefinder.org/wiki/CRT_ID
   // https://vice-emu.sourceforge.io/vice_17.html#SEC369
   // http://ist.uwaterloo.ca/~schepers/formats/CRT.TXT
   
   uint8_t* CRT_Image = MyMenuItem->Code_Image;
   MyMenuItem->ItemType = rtUnknown; //in case we fail
   uint8_t  C128Cart = false;
   
   if (memcmp(CRT_Image, "C128 CARTRIDGE", 14)==0) C128Cart = true;
   else if (memcmp(CRT_Image, "C64 CARTRIDGE", 13)!=0)
   {
      Serial.println("\"C64/128 CARTRIDGE\" not found");
      return;
   }
   
   uint32_t HeaderLen = toU32(CRT_Image+0x10);
   Serial.printf("Header len: %lu\n", HeaderLen);
   if (HeaderLen < 0x40) HeaderLen = 0x40;
   
   int16_t HWType = (int16_t)toU16(CRT_Image+0x16);
   Serial.printf("HW Type: %d ($%04x)\n", HWType, (uint16_t)HWType);
   switch (HWType)
      {
      case Cart_Generic:
         //leave IOH as default/user set for generic
         //IO1[rwRegNextIOHndlr] = IOH_None;  
         break;
      case Cart_MIDI_Datel:
         IO1[rwRegNextIOHndlr] = IOH_MIDI_Datel;
         break;
      case Cart_MIDI_Sequential:
         IO1[rwRegNextIOHndlr] = IOH_MIDI_Sequential;
         break;
      case Cart_MIDI_Passport:
         IO1[rwRegNextIOHndlr] = IOH_MIDI_Passport;
         break;
      case Cart_MIDI_Namesoft:
         IO1[rwRegNextIOHndlr] = IOH_MIDI_NamesoftIRQ;
         break;
      case Cart_SwiftLink:
         IO1[rwRegNextIOHndlr] = IOH_Swiftlink;
         break;
      case Cart_EpyxFastload:
         IO1[rwRegNextIOHndlr] = IOH_EpyxFastLoad;
         break;
      default:
         Serial.println("Unknown Cart HW Type");
         return;
      }
   
   uint8_t EXROM = CRT_Image[0x18];
   uint8_t GAME = CRT_Image[0x19];
   Serial.printf("EXROM: %d\n", EXROM);
   Serial.printf(" GAME: %d\n", GAME);
   
   Serial.printf("Name: %s\n", (CRT_Image+0x20));
   
   uint8_t *ChipImage = CRT_Image+HeaderLen;
   //On to CHIP packet(s)...
   if (memcmp(ChipImage, "CHIP", 4)!=0)
   {
      Serial.println("\"CHIP\" not found");
      return;
   }
  
   Serial.printf("Packet len: $%08x\n",  toU32(ChipImage+0x04));
   Serial.printf("Chip Type: %d\n",      toU16(ChipImage+0x08));
   Serial.printf(" Bank Num: %d\n",      toU16(ChipImage+0x0A));
   uint16_t LoadAddress = toU16(ChipImage+0x0C);
   uint16_t ROMSize = toU16(ChipImage+0x0E);
   Serial.printf("Load Addr: $%04x\n",   LoadAddress);
   Serial.printf(" ROM Size: $%04x\n",   ROMSize);
      
   //We have a good CRT image!
   //Is it a config we support?
   MyMenuItem->Code_Image += HeaderLen+0x10;
   
   if(HWType==Cart_EpyxFastload && LoadAddress == 0x8000 && ROMSize == 0x2000) //sets EXROM & GAME high in crt
   {
      MyMenuItem->ItemType = rtBin8kLo;
      Serial.println("\n 8kLo config");
      return;
   }
   
   if(EXROM==0 &&            LoadAddress == 0x8000 && ROMSize == 0x2000) //GAME is usually==1, Centiped calls for low but doesn't use it
   {
      MyMenuItem->ItemType = rtBin8kLo;
      Serial.println("\n 8kLo config");
      return;
   }      

   if(EXROM==1 && GAME==0 && LoadAddress == 0xe000 && ROMSize == 0x2000)
   {
      MyMenuItem->ItemType = rtBin8kHi;
      Serial.println("\n 8kHi config");
      return;
   }      

   if(EXROM==0 && GAME==0 && LoadAddress == 0x8000 && ROMSize == 0x4000)
   {
      MyMenuItem->ItemType = rtBin16k;
      Serial.println("\n 16k config");
      return;
   }      
   
   if(EXROM==0 && GAME==0 && LoadAddress == 0x0000 && ROMSize == 0x2000 && C128Cart)
   {
      MyMenuItem->ItemType = rtBinC128;
      Serial.println("\n C128 config");
      return;
   }      
   
   Serial.println("\nHW config unknown!");
}

uint32_t toU32(uint8_t* src)
{
   return
      ((uint32_t)src[0]<<24) + 
      ((uint32_t)src[1]<<16) + 
      ((uint32_t)src[2]<<8 ) + 
      ((uint32_t)src[3]    ) ;
}

uint16_t toU16(uint8_t* src)
{
   return
      ((uint16_t)src[0]<<8 ) + 
      ((uint16_t)src[1]    ) ;
}

