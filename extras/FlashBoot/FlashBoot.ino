/*
  Copyright (c) 2017 Arduino LLC.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <SerialFlash.h>
#include <FlashStorage.h>
#include <CRC32.h>				 // Add CRC engine

#define SDU_START    0x2000
#define SDU_SIZE     0x4000

#define SKETCH_START (uint32_t*)(SDU_START + SDU_SIZE)

#ifndef FLASH_CS_PIN
#define FLASH_CS_PIN  10
#endif

#define UPDATE_FILE "UPDATE.BIN"
#define KNOW_CHECKSUM_FILE "KNOW_CHECKSUM.BIN"



FlashClass flash;


CRC32 crc;

// Initialize C library
extern "C" void __libc_init_array(void);

int main() {
	init();

	__libc_init_array();

	delay(1);

	bool updateFlashed = false;

	if (SerialFlash.begin(FLASH_CS_PIN) && SerialFlash.exists(UPDATE_FILE)) 
	{
		crc.reset(); // Reset the CRC engine		
		
		SerialFlashFile updateFile = SerialFlash.open(UPDATE_FILE);

		if (updateFile) // File opened successfully?
		{
			unsigned long updateSize = updateFile.size(); // Get update file size

			unsigned long byteCount = 0;

			uint8_t serialFlashBuffer[512];
			size_t n;

			while (byteCount < updateSize)
			{

				n = updateFile.read(serialFlashBuffer, sizeof(serialFlashBuffer));

				for (size_t i = 0; i < n; i++)
				{
					crc.update(serialFlashBuffer[i]);
				}

				byteCount = byteCount + n;
	
			}

			uint32_t checksum = crc.finalize(); // Calculate the final CRC32 checksum.
	
			updateFile.close();

			if (SerialFlash.exists(KNOW_CHECKSUM_FILE))
			{

				SerialFlashFile know_checksum_file = SerialFlash.open(KNOW_CHECKSUM_FILE);

				uint8_t tmpBuffer[4];

				if (know_checksum_file) // File opened successfully?
				{
					know_checksum_file.read(tmpBuffer, 4);

					know_checksum_file.close();

					uint8_t calculated_checksum[4];

					calculated_checksum[3] = checksum;
					calculated_checksum[2] = checksum >> 8;
					calculated_checksum[1] = checksum >> 16;
					calculated_checksum[0] = checksum >> 24;

					if (memcmp(calculated_checksum, tmpBuffer, sizeof(calculated_checksum)) == 0)
					{
						// CRC match we can now update
					
						updateFile = SerialFlash.open(UPDATE_FILE);
						
						if (updateFile) // File opened successfully?
						{
							if (updateSize > SDU_SIZE)
							{
								// skip the SDU section
								updateFile.seek(SDU_SIZE);
								updateSize -= SDU_SIZE;

								uint32_t flashAddress = (uint32_t)SKETCH_START;

								// erase the pages
								flash.erase((void*)flashAddress, updateSize);

								uint8_t buffer[512];

								// write the pages
								for (uint32_t i = 0; i < updateSize; i += sizeof(buffer))
								{
									updateFile.read(buffer, sizeof(buffer));

									flash.write((void*)flashAddress, buffer, sizeof(buffer));

									flashAddress += sizeof(buffer);
								}

								updateFile.close();

								updateFlashed = true;
							}
						}
					}
					else
					{
						// CRC does not match do not update!
					}
				}
				
				if (updateFlashed) {
					SerialFlash.remove(UPDATE_FILE);
				}
			}
		}
	}

	// jump to the sketch
	__set_MSP(*SKETCH_START);

	//Reset vector table address
	SCB->VTOR = ((uint32_t)(SKETCH_START)& SCB_VTOR_TBLOFF_Msk);

	// address of Reset_Handler is written by the linker at the beginning of the .text section (see linker script)
	uint32_t resetHandlerAddress = (uint32_t) * (SKETCH_START + 1);
	// jump to reset handler
	asm("bx %0"::"r"(resetHandlerAddress));

}

