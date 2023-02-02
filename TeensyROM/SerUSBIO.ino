
void ServiceSerial()
{
   uint8_t inByte = Serial.read();
   if (inByte == 0x64) //command from app
   {
      inByte = Serial.read();
      switch (inByte)
      {
         case 0x55:
            Serial.println("TeensyROM Ready!");
            break;
         case 0xAA:
            ReceiveFile();        
            break;
         case 0xEE:
            Serial.println("Reset cmd received");
            SetUpMainMenuROM();
            doReset = true;
            break;
         case 0x67:
            root = SD.open("/");
            if(root) printDirectory(root);  
            else Serial.println("SD Card not present/initialized");
            break;
         default:
            Serial.printf("Unk: %02x\n", inByte); 
            break;
      }
   }
}

void ReceiveFile()
{ 
      //   App: SendFileToken 0x64AA
      //Teensy: ack 0x6464
      //   App: Send Length(2), CS(2), Name(MAX_ROMNAME_CHARS 25, incl term), file(length)
      //Teensy: Pass 0x6480 or Fail 0x9b7f

      //send file token has been received, only 2 byte responses until final response
   
   Serial.write(0x64);  //ack
   Serial.write(0x64);  
   ROMMenu[0].HW_Config = rtNone;  //in case we fail
   
   if(!SerialAvailabeTimeout()) return;
   uint16_t len = Serial.read();
   len = len + 256 * Serial.read();
   if(!SerialAvailabeTimeout()) return;
   uint16_t CheckSum = Serial.read();
   CheckSum = CheckSum + 256 * Serial.read();
   
   for (int i = 0; i < MAX_ROMNAME_CHARS; i++) 
   {
      if(!SerialAvailabeTimeout()) return;
      ROMMenu[0].Name[i] = Serial.read();
   }
   
   uint16_t bytenum = 0;
   while(bytenum < len)
   {
      if(!SerialAvailabeTimeout()) return;
      RAM_Image[bytenum] = Serial.read();
      CheckSum-=RAM_Image[bytenum++];

   }  
   if (CheckSum!=0)
   {  //Failed
     Serial.write(0x9B);  // 155
     Serial.write(0x7F);  // 127
     Serial.printf("Failed! Len:%d, RCS:%d, Name:%s\n", len, CheckSum, ROMMenu[0].Name);
     //for (int i = 0; i < MAX_ROMNAME_CHARS; i++) Serial.printf("%02d-%d\n", i, ROMMenu[0].Name[i]);
     return;
   }   

   //success!
   Serial.write(0x64);  
   Serial.write(0x80);  
   Serial.printf("%s received succesfully\n", ROMMenu[0].Name);
   
   ROMMenu[0].Size = len;  
   
   //check extension
   if (strcmp((ROMMenu[0].Name + strlen(ROMMenu[0].Name) - 4), ".prg")==0)
   {
      ROMMenu[0].HW_Config = rtPrg;
      Serial.println(".PRG file detected");
      return;
   }
   
   if (strcmp((ROMMenu[0].Name + strlen(ROMMenu[0].Name) - 4), ".crt")!=0)
   {
      //HW_Config default to rtNone, set at start
      Serial.println("File type unknown!");
      return;
   }
   
   
   Serial.println(".CRT file detected");
   //https://vice-emu.sourceforge.io/vice_17.html#SEC369
   //http://ist.uwaterloo.ca/~schepers/formats/CRT.TXT
   
   if (memcmp(RAM_Image, "C64 CARTRIDGE", 13)!=0)
   {
      Serial.println("\"C64 CARTRIDGE\" not found");
      return;
   }
   
   uint32_t HeaderLen = toU32(RAM_Image+0x10);
   Serial.printf("Header len: %lu\n", HeaderLen);
   if (HeaderLen < 0x40) HeaderLen = 0x40;
   
   Serial.printf("HW Type: %d\n", toU16(RAM_Image+0x16));
   if (toU16(RAM_Image+0x16) !=0)
   {
      Serial.println("Only \"Normal\" carts *currently* supported");
      return;
   }
   
   uint8_t EXROM = RAM_Image[0x18];
   uint8_t GAME = RAM_Image[0x19];
   Serial.printf("EXROM: %d\n", EXROM);
   Serial.printf(" GAME: %d\n", GAME);
   
   Serial.printf("Name: %s\n", (RAM_Image+0x20));
   
   uint8_t *ChipImage = RAM_Image+HeaderLen;
   //On to CHIP packet(s)...
   if (memcmp(ChipImage, "CHIP", 4)!=0)
   {
      Serial.println("\"CHIP\" not found");
      return;
   }
  
   Serial.printf("Packet len: $%08x\n",  toU32(ChipImage+0x04));
   Serial.printf("Chip Type: %d\n",      toU16(ChipImage+0x08));
   Serial.printf(" Bank Num: %d\n",      toU16(ChipImage+0x0A));
   Serial.printf("Load Addr: $%04x\n",   toU16(ChipImage+0x0C));
   Serial.printf(" ROM Size: $%04x\n",   toU16(ChipImage+0x0E));

   ROMMenu[0].Code_Image=RAM_Image+HeaderLen+0x10;
   
   if(EXROM==0 && GAME==1 && toU16(ChipImage+0x0C) == 0x8000 && toU16(ChipImage+0x0E) == 0x2000)
   {
      ROMMenu[0].HW_Config = rt8kLo;
      Serial.println("\n 8kLo config");
      return;
   }      

   if(EXROM==1 && GAME==0 && toU16(ChipImage+0x0C) == 0xe000 && toU16(ChipImage+0x0E) == 0x2000)
   {
      ROMMenu[0].HW_Config = rt8kHi;
      Serial.println("\n 8kHi config");
      return;
   }      

   if(EXROM==0 && GAME==0 && toU16(ChipImage+0x0C) == 0x8000 && toU16(ChipImage+0x0E) == 0x4000)
   {
      ROMMenu[0].HW_Config = rt16k;
      Serial.println("\n 16k config");
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

bool SerialAvailabeTimeout()
{
   uint32_t StartTOMillis = millis();
   
   while(!Serial.available() && (millis() - StartTOMillis) < SerialTimoutMillis); // timeout loop
   if (Serial.available()) return(true);
   
   Serial.write(0x9B);  
   Serial.write(0x7F);  
   Serial.print("Timeout!\n");  
   return(false);
}

#define MaxDirEntries  250
void printDirectory(File dir) 
{
   uint16_t Count = 0;
   while(Count < MaxDirEntries) 
   {
     File entry = dir.openNextFile();
     if (! entry) return;
     if (entry.isDirectory()) Serial.print("/");
     Serial.printf("%d- %s\n", Count++, entry.name());  
     entry.close();
   }
   Serial.print("Too many files!");
}




        