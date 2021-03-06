#include "main.h"

extern struct ICAR_DEVICE my_icar;

#define FLASH_BOOT_ADDR  ((uint32_t)0x08000000)
#define FLASH_BOOT_SIZE  ((uint32_t)0x1000) //4KB, page0~3

#define FLASH_APP_ADDR  ((uint32_t)0x08001000)
#define FLASH_APP_SIZE  ((uint32_t)0xF000) //60KB, page4~63

#define FLASH_CFG_ADDR  ((uint32_t)0x08010000)
#define FLASH_CFG_SIZE  ((uint32_t)0x800)  //2KB,  page64~65

#define FLASH_IDX_ADDR  ((uint32_t)0x08010800)
#define FLASH_IDX_SIZE  ((uint32_t)0x400)  //1KB,  page66

#define FLASH_CRC_ADDR  ((uint32_t)0x08010C00)
#define FLASH_CRC_SIZE  ((uint32_t)0x400)  //1KB,  page67

#define FLASH_DAT_ADDR  ((uint32_t)0x08011000)
#define FLASH_DAT_SIZE  ((uint32_t)0xF000) //60KB, page68~127

//return 0 : OK, else error
unsigned char flash_erase( uint32_t addr )
{
	FLASH_Status FLASHStatus = FLASH_COMPLETE;

	/* Unlock the Flash Bank1 Program Erase controller */
	FLASH_UnlockBank1();

	/* Clear All pending flags */
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	
	//Erase the FLASH pages 
	FLASHStatus = FLASH_ErasePage(addr);
	FLASH_LockBank1();

	if ( FLASHStatus == FLASH_COMPLETE ) {
		prompt("FLASH_ErasePage :%08X success.\r\n",addr);
		return 0 ;
	}
	else {
		prompt("FLASH_ErasePage :%08X failure: %d\r\n",addr,FLASHStatus);
		prompt("Check %s:%d\r\n",__FILE__,__LINE__);
		return 1 ;
	}
}

//return 0 : OK, else return failure 
unsigned char flash_prog_u16( uint32_t addr, uint16_t data)
{
	FLASH_Status FLASHStatus = FLASH_COMPLETE;
	unsigned char retry = MAX_PROG_TRY ;

	/* Unlock the Flash Bank1 Program Erase controller */
	FLASH_UnlockBank1();

	/* Clear All pending flags */
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	
	/* Program Flash Bank1 */
	while ( retry-- ) {
		FLASHStatus = FLASH_ProgramHalfWord(addr, data);
		if ( *(vu16*)(addr) == data ) {//prog success
			FLASH_LockBank1();
			if ( my_icar.debug > 1) {
				prompt("Prog %04X @ %08X, read back: %04X\r\n",data,addr,\
					*(vu16*)(addr));
			}
			return 0 ;
		}
		else {
			prompt("Prog %04X @ %08X failure! read back: %04X\r\n",\
					data,addr,*(vu16*)(addr));
		}
	}

	//have try "retry" timer, still failure, return this address
	FLASH_LockBank1();

	prompt("Prog add :%08X failure: %d\r\n",addr,FLASHStatus);
	prompt("Check %s:%d\r\n",__FILE__,__LINE__);
	my_icar.upgrade.prog_fail_addr = addr ;
	return 1 ;
}

//0: ok, others: error
unsigned char flash_upgrade_ask( unsigned char *buf) 
{//Ask upgrade data ...

	unsigned char blk_cnt;
	unsigned short var_u16, crc16;
	unsigned int var_u32, fw_size ;

	//TBD: Check error flag, feedback to server if error
	//set buf[5] > 0xF0 if error, then report to server for failure detail.

	//check upgrade process status
	if ( *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) == 0xFFFF ) {//empty
		prompt("New FW empty, can be upgrade...\r\n");
	}
	else {//Upgrading...
		prompt("In upgrade %d ==> %d process...\r\n", my_icar.fw_rev,\
				*(vu16*)(my_icar.upgrade.base+NEW_FW_REV));

		//Ask firmware data
		//Calc. block:
		fw_size = *(vu32*)(my_icar.upgrade.base+NEW_FW_SIZE) ;
		prompt("Firmware size: %X\t", fw_size);

		if((fw_size%1024) > 0 ){
			blk_cnt = (fw_size >> 10) + 1;
		}
		else{
			blk_cnt = (fw_size >> 10);
		}
		printf("Block count: %d\r\n",blk_cnt);

		//Check blk crc empty? ask data if empty
		//C9 97 55 00 xx yy 
		//xx: data len, 
		//yy: KB sequence, 01: ask 1st KB of FW, 02: 2nd KB, 03: 3rd KB

		for ( buf[5] = 1 ; buf[5] <= blk_cnt ; buf[5]++ ) {
			crc16 = *(vu16*)(my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8) ;
			printf(".");
			//debug_flash("BLK %d CRC: %X @ %08X %d\r\n", buf[5],crc16,my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8,__LINE__);
			if ( crc16 == 0xFFFF ) { //empty
				//Check ~CRC again
				crc16 = *(vu16*)(my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8+4) ;
				if ( crc16 == 0 ) {//the CRC result just 0xFFFFFFFF
					;//prompt("Block %d CRC: %X @ %08X\t", buf[5],crc_dat,my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8);
				}
				else { 
					if ( crc16 == 0xFFFF ) { //empty
						printf("\r\n");
						prompt("Block %d CRC: %X @ %08X\t", buf[5],crc16,my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8);
						printf("Ask BLK %d data...\r\n",buf[5]);

						//Send : BLK index + new firmware revision
						//C9 00 55 00 03 01 00 5E
						buf[3] = 0;//length high
						buf[4] = 3;//length low
						
						buf[6] = (*(vu16*)(my_icar.upgrade.base+NEW_FW_REV))>>8&0xFF;  //fw rev. high
						buf[7] = (*(vu16*)(my_icar.upgrade.base+NEW_FW_REV))&0xFF;//fw rev. low
						return 0 ;
					}
					else {//BLK_CRC_DAT+buf[5]*8 is FF but 
						  //BLK_CRC_DAT+buf[5]*8+4 is not FF, and not 0, the flash failure?

						//erase the info blk, re-get all data
						flash_erase(my_icar.upgrade.base);
						printf("\r\nFlash failure, check: %s: %d\r\n",__FILE__, __LINE__);
					}
				}
			}
			else { //crc_dat != 0xFFFF
				//Check ~CRC again
				if ( crc16 == (~(*(vu16*)(my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8+4))&0xFFFF)){
					// correct
					//debug_flash("Block %d CRC: %X @ %08X Have data, no need ask\r\n", buf[5],crc16,\
							my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8);
				}
				else { // crc result un-correct
					//erase this block and info blk
					prompt("Block %d CRC: %X @ %08X ERR! %d\r\n", buf[5],\
						*(vu16*)(my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8),\
						my_icar.upgrade.base+BLK_CRC_DAT+buf[5]*8,__LINE__);

					flash_erase(my_icar.upgrade.base+0x400*buf[5]);
					flash_erase(my_icar.upgrade.base);
				}
			}
		}//end block check

		// Have download all firmware from server, now verify it
		printf("\r\n");
		prompt("Got all fw data! check: %s: %d\r\n",__FILE__, __LINE__);

		// Check whether had verified?
		if ( (*(vu32*)(my_icar.upgrade.base+FW_READY_ADD) == 0xFFFFFFFF) &&\
			(*(vu32*)(my_icar.upgrade.base+FW_READY_ADD+4) == 0xFFFFFFFF) ) {

			//empty, check CRC...
	
			//Calc CRC for local fw in flash
			var_u32 = 0 ;
			for ( var_u16 = 0 ; var_u16 < fw_size ; var_u16 ++ ) {
				//blk_cnt = *(vu8*)(my_icar.upgrade.base+0x400+var_u16);
				//var_u32 = crc16tablefast(&blk_cnt,1,var_u32);
				//debug_flash("i:%X\t%02X\t%04X\r\n",var_u16,blk_cnt,var_u32&0xFFFF);
				var_u32 = crc16tablefast((unsigned char *)my_icar.upgrade.base+0x400+var_u16,1,var_u32);
			}

			debug_flash("Local FW, Calc %d Bytes, CRC: %04X\r\n",var_u16, var_u32&0xFFFF);

			if ( (var_u32&0xFFFF) == *(vu16*)(my_icar.upgrade.base+FW_CRC_DAT) ) {
	
				//correct, prog the ready flag to flash
				var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_READY_ADD,FW_READY_FLAG&0xFFFF);
				if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure
	
				var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_READY_ADD+2,(FW_READY_FLAG>>16)&0xFFFF);
				if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure
	
				var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_READY_ADD+4,~(FW_READY_FLAG&0xFFFF));
				if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure
	
				var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_READY_ADD+6,~((FW_READY_FLAG>>16)&0xFFFF));
				if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure
					
				my_icar.upgrade.status = UPGRADED ;//app_task will check this flag and reboot
			}
			else { //FW crc result un-correct, earse all data
				prompt("CRC in flash is: %04X FW CRC ERR, erase all data! %s: %d\r\n",\
					*(vu16*)(my_icar.upgrade.base+FW_CRC_DAT),__FILE__,__LINE__);

				//Just need erase the info blk, re-get all data
				flash_erase(my_icar.upgrade.base);//2013/1/3 10:44:23

				//Record then upload err to server
				//BKP_DR1, ERR index: 	15~12:MCU reset 
				//						11~8:upgrade fw failure code
				//						7~4:GPRS disconnect reason
				//						3~0:GSM module poweroff reason
				var_u16 = (BKP_ReadBackupRegister(BKP_DR1))&0xF0FF;
				var_u16 = var_u16 | (ERR_UPGRADE_FW_CRC<<8) ;
			    BKP_WriteBackupRegister(BKP_DR1, var_u16);

				//BKP_DR6, upgrade fw time(UTC Time) high
				//BKP_DR7, upgrade fw time(UTC Time) low
			    BKP_WriteBackupRegister(BKP_DR6, ((RTC_GetCounter( ))>>16)&0xFFFF);//high
			    BKP_WriteBackupRegister(BKP_DR7, (RTC_GetCounter( ))&0xFFFF);//low
			}

			prompt("Flag in flash is: %08X\r\n",\
							*(vu32*)(my_icar.upgrade.base+FW_READY_ADD));
		}
		else { //FW_READY flag no empty

			//check flag correct?
			if ( (*(vu32*)(my_icar.upgrade.base+FW_READY_ADD) == FW_READY_FLAG) &&\
				(*(vu32*)(my_icar.upgrade.base+FW_READY_ADD+4) == ~FW_READY_FLAG ) ) {

				my_icar.upgrade.status = UPGRADED ;//app_task will check this flag and reboot
			}
			else {
				prompt("FW_READY_ADD had been modified: %08X %s:%d\r\n",\
					*(vu32*)(my_icar.upgrade.base+FW_READY_ADD),\
					__FILE__,__LINE__);

				//TBD: if these data are usefule buffer? ...
				//now just erase this sector
				flash_erase(my_icar.upgrade.base);//2013/1/3 10:44:41

				//Record then upload err to server
				//BKP_DR1, ERR index: 	15~12:MCU reset 
				//						11~8:upgrade fw failure code
				//						7~4:GPRS disconnect reason
				//						3~0:GSM module poweroff reason
				var_u16 = (BKP_ReadBackupRegister(BKP_DR1))&0xF0FF;
				var_u16 = var_u16 | (ERR_UNEXPECT_READY_FLAG<<8) ;
			    BKP_WriteBackupRegister(BKP_DR1, var_u16);

				//BKP_DR6, upgrade fw time(UTC Time) high
				//BKP_DR7, upgrade fw time(UTC Time) low
			    BKP_WriteBackupRegister(BKP_DR6, ((RTC_GetCounter( ))>>16)&0xFFFF);//high
			    BKP_WriteBackupRegister(BKP_DR7, (RTC_GetCounter( ))&0xFFFF);//low
			}
		}

		return 1;
	}

	//Default CMD: send current hardware and firmware revision
	//C9 21 55 00 04 00 00 00 5E E7
	buf[3] = 0;//length high
	buf[4] = 4;//length low
	
	buf[5] = 0x00 ;//00: mean buf[6] is hw rev, others: block seq
	buf[6] = my_icar.hw_rev;
	buf[7] = (my_icar.fw_rev>>8)&0xFF;//fw rev. high
	buf[8] = (my_icar.fw_rev)&0xFF;//fw rev. low

	return 0;
}

//Return 0: ok, others error.
unsigned char flash_upgrade_rec( unsigned char *buf, unsigned char *buf_start) 
{//receive upgrade data ...
	unsigned short buf_index, buf_len, fw_rev, fw_crc16 ;
	unsigned char buf_type ;
	unsigned int var_u32, fw_size;

	//Head SEQ CMD Len Status INFO CRC16
	//DE 01 D5 00 08 
	//00 00 00 DA => status, indicate, FW rev
	//A8 3C xx xx 15 FE => FW len(4B), FW CRC16
	//1A 59  =>CRC16

	//C9 57 D5 00 xx yy data
	//xx: data len, 
	//yy: KB sequence, 00: data is latest firmware revision(u16) + size(u16)
	//                 01: 1st KB of FW, 02: 2nd KB, 03: 3rd KB

	if ( (buf+4) < buf_start+GSM_BUF_LENGTH ) {
		buf_len = *(buf+4);
	}
	else {
		buf_len = *(buf+4-GSM_BUF_LENGTH);
	}

	if ( (buf+3) < buf_start+GSM_BUF_LENGTH ) {
		buf_len = ((*(buf+3))<<8) | buf_len;
	}
	else {
		buf_len = ((*(buf+3-GSM_BUF_LENGTH))<<8) | buf_len;
	}

	debug_flash("Len= %d : ",buf_len);

	if ( buf_len < 5 || buf_len > 1024+6) { 
		//err, Min.: status + 00 + rev(2Bytes) + size(2B) = 6 Bytes
		//Max. block:status + index(1 Byte) + fw_rev(2B) + fw_data(1024 Bytes) +CRC(2B)
		printf("\r\n");
		prompt("Length: %d is un-correct! Check %s: %d\r\n",\
				buf_len,__FILE__,__LINE__);
		return ERR_UPGRADE_BUFFER_LEN ;
	}

	//extract 	fw_rev = buf[7] << 8 | buf[8];
	if ( (buf+8) < buf_start+GSM_BUF_LENGTH ) {
		fw_rev = *(buf+8);
	}
	else {
		fw_rev = *(buf+8-GSM_BUF_LENGTH);
	}

	if ( (buf+7) < buf_start+GSM_BUF_LENGTH ) {
		fw_rev = ((*(buf+7))<<8) | fw_rev;
	}
	else {
		fw_rev = ((*(buf+7-GSM_BUF_LENGTH))<<8) | fw_rev;
	}

	debug_flash("New fw rev: %d\r\n",fw_rev);
	
	//check firmware revision
	if ( fw_rev <=  my_icar.fw_rev ) {//firmware old
		prompt("Error, server fw : %d is older then current: %d\r\n",fw_rev,my_icar.fw_rev);
		return ERR_UPGRADE_HAS_LATEST_FW;
	}

	// buf[6] is indicate buffer type
	if ( (buf+6) < buf_start+GSM_BUF_LENGTH ) {
		buf_type = *(buf+6);
	}
	else {
		buf_type = *(buf+6-GSM_BUF_LENGTH);
	}

	if ( buf_type == 0 ){//FW rev&size info
		//C9 20 D5 00 05 00 FF FF FF FF 39
	
		//extract fw_size= buf[9]<<24 | buf[10]<<16 | buf[11]<<8 | buf[12];
		if ( (buf+12) < buf_start+GSM_BUF_LENGTH ) {
			fw_size = *(buf+12);
		}
		else {
			fw_size = *(buf+12-GSM_BUF_LENGTH);
		}
	
		if ( (buf+11) < buf_start+GSM_BUF_LENGTH ) {
			fw_size = ((*(buf+11))<<8) | fw_size;
		}
		else {
			fw_size = ((*(buf+11-GSM_BUF_LENGTH))<<8) | fw_size;
		}
	
		if ( (buf+10) < buf_start+GSM_BUF_LENGTH ) {
			fw_size = ((*(buf+10))<<16) | fw_size;
		}
		else {
			fw_size = ((*(buf+10-GSM_BUF_LENGTH))<<16) | fw_size;
		}

		if ( (buf+9) < buf_start+GSM_BUF_LENGTH ) {
			fw_size = ((*(buf+9))<<24) | fw_size;
		}
		else {
			fw_size = ((*(buf+9-GSM_BUF_LENGTH))<<24) | fw_size;
		}

		debug_flash("New fw size: %d\r\n",fw_size);
	
		//check firmware size
		if ( fw_size > 60*1024 ) {//must < 60KB, can be change if use large flash MCU
			prompt("Error, firmware size: %d Bytes> 60KB\r\n",fw_size);
			return ERR_UPGRADE_SIZE_LARGE;
		}

		//extract 	fw_crc16= buf[14] ~ buf[13];
		if ( (buf+14) < buf_start+GSM_BUF_LENGTH ) {
			fw_crc16 = *(buf+14);
		}
		else {
			fw_crc16 = *(buf+14-GSM_BUF_LENGTH);
		}
	
		if ( (buf+13) < buf_start+GSM_BUF_LENGTH ) {
			fw_crc16 = ((*(buf+13))<<8) | fw_crc16;
		}
		else {
			fw_crc16 = ((*(buf+13-GSM_BUF_LENGTH))<<8) | fw_crc16;
		}
	
		debug_flash("New fw crc16: 0x%04X\r\n",fw_crc16);
			
		//check firmware revision
		if ( *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) == 0xFFFF ) {//empty

			debug_flash("FLASH UPGRADE: new FW rev empty...\r\n");
			//prog FW REV
			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_REV,fw_rev);
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			//will check again before upgrade, prevent flase failure
			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_REV+4,~fw_rev);
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			//prog FW SIZE
			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_SIZE+0,fw_size&0xFFFF);
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_SIZE+2,(fw_size>>16)&0xFFFF);
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_SIZE+4,~(fw_size&0xFFFF));
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			var_u32 = flash_prog_u16(my_icar.upgrade.base+NEW_FW_SIZE+6,~((fw_size>>16)&0xFFFF));
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			//prog FW CRC16 to flash
			var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_CRC_DAT+0,fw_crc16&0xFFFF);
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

			var_u32 = flash_prog_u16(my_icar.upgrade.base+FW_CRC_DAT+4,~(fw_crc16&0xFFFF));
			if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

		}
		else { //upgrading...
			//check upgrading rev is same as new rev?
			if ( *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) == fw_rev ) {//same

				prompt("Firmware %d ==> %d upgrading...",my_icar.fw_rev,fw_rev);
			}
			else {//difference, case:升级过程中，又有新版本发布
				if ( fw_rev > *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) )  {//newer
					prompt("Newer Firmware %d ==> %d upgrading...",my_icar.fw_rev,fw_rev);
					flash_erase(my_icar.upgrade.base);//next time will re-start prog.
		
					//TBD if prog error....
				}
				else {//older, maybe something wrong
					prompt("Error, server fw %d is older than upgrading firmware: %d",\
							fw_rev, *(vu16*)(my_icar.upgrade.base+NEW_FW_REV));
					printf("\t exit!\r\n");
					return ERR_UPGRADE_UP_NEWER;
				}
			}
		}
	}//end of if ( buf_type == 0 ){//FW rev&size info
	else { //buf_type != 0, each block data
		//check FW Rev info in flash
		if ( *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) == 0xFFFF ) {//empty

			prompt("Error, no firmware info in flash\r\n");
			return ERR_UPGRADE_NO_INFO ;
		}
		else { //have info, check info is same as block data?
			if ( *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) == fw_rev ) {//same

				//same, Check CRC16

				//Max. block:status + index(1 Byte) + fw_rev(2B) + fw_data(1024 Bytes) +CRC(2B)
				var_u32 = 0 ;
				for ( buf_index = 0 ; buf_index < (buf_len-6); buf_index++) {

					if ( (buf+buf_index+9) < buf_start+GSM_BUF_LENGTH ) {
						var_u32 = crc16tablefast(buf+buf_index+9 , 1, var_u32);
					}
					else {//data in begin of buffer
						var_u32 = crc16tablefast(buf+buf_index+9-GSM_BUF_LENGTH , 1, var_u32);
					}
				}

				//extract CRC value
				if (( buf+buf_index+9) < buf_start+GSM_BUF_LENGTH ) {
					fw_crc16 = (*(buf+buf_index+9))<<8;
				}
				else {//data in begin of buffer
					fw_crc16 = (*(buf+buf_index+9-GSM_BUF_LENGTH))<<8;
				}

				if (( buf+buf_index+10) < buf_start+GSM_BUF_LENGTH ) {
					fw_crc16 = (*(buf+buf_index+10))|fw_crc16;
				}
				else {//data in begin of buffer
					fw_crc16 = (*(buf+buf_index+10-GSM_BUF_LENGTH))|fw_crc16;
				}

				if ( (var_u32&0xFFFF) == fw_crc16 ) {//CRC same
					prompt("OK, Block %d CRC :%08X correct.\r\n",buf_type, var_u32);

					//erase previous contents first
					if ( my_icar.upgrade.page_size == 0x400 ) {
						flash_erase(my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type);
					}
					else { //2K per block
						if ( buf_type%1 == 0 ) {
							flash_erase(my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type);
						}
					}
					//prog data to flash: C9 F3 D5 00 07 01 00 7D 00 01 FF FF 95
					//use fw_size to save fw data temporary
					for ( buf_index = 0 ; buf_index < (buf_len-6)/2; buf_index++) {
	
						if ( (buf+buf_index*2+10) < buf_start+GSM_BUF_LENGTH ) {
							fw_size = (*(buf+buf_index*2+10))<<8;
						}
						else {//data in begin of buffer
							fw_size = (*(buf+buf_index*2+10-GSM_BUF_LENGTH))<<8;
						}
	
						if ( (buf+buf_index*2+9) < buf_start+GSM_BUF_LENGTH ) {
							fw_size = ((*(buf+buf_index*2+9)))|fw_size;
						}
						else {//data in begin of buffer
							fw_size = ((*(buf+buf_index*2+9-GSM_BUF_LENGTH)))|fw_size;
						}

						/* Reload IWDG counter */
						IWDG_ReloadCounter();  

						//ok for 1K or 2K blk flash
						var_u32 = flash_prog_u16(my_icar.upgrade.base+0x400*buf_type+buf_index*2,fw_size&0xFFFF);

						if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

						if ( buf_index < 4 ) {
							debug_flash("Page:%d, %08X + %02X: %02X %02X\r\n",\
								(my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type-0x08000000)/my_icar.upgrade.page_size,\
								my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type, buf_index*2,\
								*(vu8*)(my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type+buf_index*2),\
								*(vu8*)(my_icar.upgrade.base+my_icar.upgrade.page_size*buf_type+buf_index*2+1));
						}
					}

					//prog CRC result to flash
					var_u32 = flash_prog_u16(my_icar.upgrade.base+BLK_CRC_DAT+buf_type*8,fw_crc16);
					if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

					var_u32 = flash_prog_u16(my_icar.upgrade.base+BLK_CRC_DAT+buf_type*8+4,~(fw_crc16));
					if ( var_u32 ) return ERR_UPGRADE_PROG_FAIL; //prog failure

					debug_flash("CRC in flash is: %04X",\
						*(vu16*)(my_icar.upgrade.base+BLK_CRC_DAT+buf_type*8));

				}
				else {//CRC different
					prompt("SW CRC: %04X, Buf CRC: %04X\r\n",(var_u32&0xFFFF),fw_crc16);
					prompt("Error, Block %d CRC ERR, check %s: %d\r\n",buf_type,__FILE__,__LINE__);
					return ERR_UPGRADE_BLK_CRC ;
				}
				//TBD
			}
			else {//difference, case:升级过程中，又有新版本发布
				if ( fw_rev > *(vu16*)(my_icar.upgrade.base+NEW_FW_REV) )  {//newer
					prompt("FW info in flash: rev %d, but in buf is %d\r\n",\
						*(vu16*)(my_icar.upgrade.base+NEW_FW_REV),fw_rev);

					prompt("Will be erase upgrade info! check %s: %d\r\n",__FILE__,__LINE__);
					flash_erase(my_icar.upgrade.base);
			
					//TBD if prog error....
				}
				else {//older, maybe something wrong
					prompt("Error, latest firmware %d is older than upgrading firmware: %d",\
							fw_rev, *(vu16*)(my_icar.upgrade.base+NEW_FW_REV));
					printf("\t exit!\r\n");
					return ERR_UPGRADE_UP_NEWER;
				}
			}
		}
	}
	
	return 0;
}

static void init_parameters( )
{
	u16 var_u16;

	//erase the parameters
	if ( my_icar.upgrade.page_size == 0x800 ) { //2KB
		flash_erase( my_icar.upgrade.base - 0x800 );
	}
	else { //1KB
		flash_erase( my_icar.upgrade.base - 0x800 );//page 65
		//flash_erase( my_icar.upgrade.base - 0x400 );//page 66
	}

	//restore to default factory value
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_REV,parameter_revision&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_REV+2,(parameter_revision>>16)&0xFFFF);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_RELAY_ON,30);//30 seconds for relay on
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_RELAY_ON+2,0);

	//for OBD CAN para:
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_STD_ID1,0x07DF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_STD_ID2,0x07E0);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_RCV_STD_ID1,0x07E8);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_EXT_ID1,(0x18DB33F1)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_EXT_ID1+2,(0x18DB33F1>>16)&0xFFFF);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_EXT_ID2,(0x18DA10F1)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_SND_EXT_ID2+2,(0x18DA10F1>>16)&0xFFFF);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_RCV_EXT_ID1,(0x18DAF111)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_RCV_EXT_ID1+2,(0x18DAF111>>16)&0xFFFF);

	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_RCV_EXT_ID2,(0xDDDDDDDD)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_OBD_CAN_RCV_EXT_ID2+2,(0xDDDDDDDD>>16)&0xFFFF);

	//Calc CRC

	/* Reset CRC generator */
	//CRC->CR = CRC_CR_RESET;

	for ( var_u16 = 0 ; var_u16 < (sizeof(my_icar.para)/4)-1 ; var_u16++ ) {
		prompt("%08X = %08X\r\n",my_icar.upgrade.base-0x800+var_u16*4, (*(vu32*)(my_icar.upgrade.base-0x800+var_u16*4)));
		CRC->DR = (*(vu32*)(my_icar.upgrade.base-0x800+var_u16*4));
	}
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_CRC,(CRC->DR)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_CRC+2,(CRC->DR>>16)&0xFFFF);
}

//get parameters from flash
void get_parameters( )
{
	u16 var_u16;

	//prompt("Parameters base address: %08X\r\n", my_icar.upgrade.base - 0x800);//-2K

	if ( *(vu32*)(my_icar.upgrade.base - 0x800 + PARA_CRC) == 0xFFFFFFFF ) {
		//empty, need initialize
		prompt("Parameters empty, need init...\r\n");
		init_parameters( );
	}

	//Check parameter revision, if no match, init...
	if ( parameter_revision != (*(vu16*)(my_icar.upgrade.base - 0x800 + PARA_REV)) ){
		prompt("Parameters revision not same, need init...\r\n");
		init_parameters( );
	}

	//verify data integrated by CRC
	/* Reset CRC generator */
	//CRC->CR = CRC_CR_RESET;

	for ( var_u16 = 0 ; var_u16 < sizeof(my_icar.para)-4 ; var_u16 += 4 ) {
		//prompt("%08X = %08X\r\n",my_icar.upgrade.base-0x800+var_u16, (*(vu32*)(my_icar.upgrade.base-0x800+var_u16)));
		CRC->DR = (*(vu32*)(my_icar.upgrade.base-0x800+var_u16));
	}

	if ( CRC->DR != (*(vu32*)(my_icar.upgrade.base-0x800+PARA_CRC ))) {
		//CRC different, data un-correct
		prompt("Err, CRC->DR: %08X but PARA_CRC: %08X\r\n",\
				CRC->DR,*(vu32*)(my_icar.upgrade.base-0x800+PARA_CRC));

		init_parameters( );
	}

	//update to RAM
	for ( var_u16 = 0 ; var_u16 < (sizeof(my_icar.para)/4)-1 ; var_u16++ ) {
		//prompt("var_u16= %04X %08X = %08X\r\n",var_u16,(unsigned int *)&my_icar.para+var_u16, \
			*((unsigned int *)&my_icar.para+var_u16));

		*((unsigned int *)&my_icar.para+var_u16) = *(vu32*)(my_icar.upgrade.base - 0x800 + var_u16*4);
	}

	if ( my_icar.para.relay_on*OS_TICKS_PER_SEC > 24*60*60 ) { // > 1 day
		my_icar.para.relay_on = (24*60*60)/OS_TICKS_PER_SEC ; //Max. is 1 day
	}

}

//Return ERR_UPDATE_SUCCESSFUL: ok, others error.
unsigned char para_update_rec( unsigned char *buf, unsigned char *buf_start) 
{//receive update data ...
	u16 buf_index, buf_len ;

	unsigned int var_u32 ;

	//C9 1F F5 00 0A 00 00 00 00 01 01 00 00 00 B4 9D
	//                ↑para offset
	//

	if ( (buf+4) < buf_start+GSM_BUF_LENGTH ) {
		buf_len = *(buf+4);
	}
	else {
		buf_len = *(buf+4-GSM_BUF_LENGTH);
	}

	if ( (buf+3) < buf_start+GSM_BUF_LENGTH ) {
		buf_len = ((*(buf+3))<<8) | buf_len;
	}
	else {
		buf_len = ((*(buf+3-GSM_BUF_LENGTH))<<8) | buf_len;
	}

	if ( my_icar.debug ) {
		prompt("Update len= %d\r\n",buf_len);
	}

	if ( buf_len < 10 || buf_len > 1024+7) { 
		//err, Min.: 00 + para(4Bytes) + xx + para(4B) = 10 Bytes

		printf("\r\n");
		prompt("Length: %d is un-correct! Check %s: %d\r\n",\
				buf_len,__FILE__,__LINE__);
		return ERR_UPDATE_BUFFER_LEN ;
	}

	//extract 	fw_rev = buf[6] << 24 | buf[7] << 16 | buf[8] << 8 | buf[9];
	if ( (buf+9) < buf_start+GSM_BUF_LENGTH ) {
		var_u32 = *(buf+9);
	}
	else {
		var_u32 = *(buf+9-GSM_BUF_LENGTH);
	}

	if ( (buf+8) < buf_start+GSM_BUF_LENGTH ) {
		var_u32 = ((*(buf+8))<<8) | var_u32;
	}
	else {
		var_u32 = ((*(buf+8-GSM_BUF_LENGTH))<<8) | var_u32;
	}

	if ( (buf+7) < buf_start+GSM_BUF_LENGTH ) {
		var_u32 = ((*(buf+7))<<16) | var_u32;
	}
	else {
		var_u32 = ((*(buf+7-GSM_BUF_LENGTH))<<16) | var_u32;
	}

	if ( (buf+6) < buf_start+GSM_BUF_LENGTH ) {
		var_u32 = ((*(buf+6))<<24) | var_u32;
	}
	else {
		var_u32 = ((*(buf+6-GSM_BUF_LENGTH))<<24) | var_u32;
	}

	//check para revision
	if ( var_u32 !=  parameter_revision ) {//para rev not same
		prompt("Error, server para rev: %d is diff than current: %d\r\n",var_u32,parameter_revision);
		return ERR_UPDATE_PARA_REV;
	}

	//show income data
	prompt("Update data: ");
	for ( buf_index = 0 ; buf_index < buf_len + 6 ; buf_index++ ) {

		if ( (buf+buf_index) < buf_start+GSM_BUF_LENGTH ) {
			printf("%02X ", *(buf+buf_index));
		}
		else {//data in begin of buffer
			printf("%02X ", *(buf+buf_index - GSM_BUF_LENGTH));
		}
	}
	printf("\r\n");

	//update the parameter in RAM
	//C9 06 F5 00 0A 00 00 00 00 00 01 00 00 00 B4 85
	for ( buf_index = 0 ; buf_index < (buf_len); buf_index=buf_index+5) {

		//get the value
		if ( (buf+buf_index+6) < buf_start+GSM_BUF_LENGTH ) {
			var_u32 =  (*(buf+buf_index+6))<<24;
		}
		else {//data in begin of buffer
			var_u32 =  (*(buf+buf_index+6 - GSM_BUF_LENGTH))<<24;
		}

		if ( (buf+buf_index+7) < buf_start+GSM_BUF_LENGTH ) {
			var_u32 =  (*(buf+buf_index+7))<<16  | var_u32;
		}
		else {//data in begin of buffer
			var_u32 =  (*(buf+buf_index+7 - GSM_BUF_LENGTH))<<16  | var_u32;
		}

		if ( (buf+buf_index+8) < buf_start+GSM_BUF_LENGTH ) {
			var_u32 =  (*(buf+buf_index+8))<<8  | var_u32;
		}
		else {//data in begin of buffer
			var_u32 =  (*(buf+buf_index+8 - GSM_BUF_LENGTH))<<8  | var_u32;
		}

		if ( (buf+buf_index+9) < buf_start+GSM_BUF_LENGTH ) {
			var_u32 =  (*(buf+buf_index+9))  | var_u32;
		}
		else {//data in begin of buffer
			var_u32 =  (*(buf+buf_index+9 - GSM_BUF_LENGTH))  | var_u32;
		}
		printf(" value: %08X\r\n", var_u32);

		//update to RAM
		//get the para index
		if ( (buf+buf_index+5) < buf_start+GSM_BUF_LENGTH ) {
			prompt("Para:%02X ", *(buf+buf_index+5));
			*((unsigned int *)&my_icar.para+(*(buf+buf_index+5))) = var_u32;
		}
		else {//data in begin of buffer
			prompt("Para:%02X ", *(buf+buf_index+5 - GSM_BUF_LENGTH));
			*((unsigned int *)&my_icar.para+(*(buf+buf_index+5 - GSM_BUF_LENGTH))) = var_u32;
		}
	}
	/*
	for ( var_u32 = 0 ; var_u32 < (sizeof(my_icar.para)/4)-1 ; var_u32++ ) {
		prompt("var_u16= %04X %08X = %08X\r\n",var_u32,(unsigned int *)&my_icar.para+var_u32, \
			*((unsigned int *)&my_icar.para+var_u32));
	}*/

	//update to flash

	//erase the parameters
	if ( my_icar.upgrade.page_size == 0x800 ) { //2KB
		//prompt("Erase 2K Page:\r\n");
		flash_erase( my_icar.upgrade.base - 0x800 );
	}
	else { //1KB
		flash_erase( my_icar.upgrade.base - 0x800 );//page 65
		//flash_erase( my_icar.upgrade.base - 0x400 );//page 66
	}

	//save to flash 

	for ( buf_index = 0 ; buf_index < (sizeof(my_icar.para)/4)-1 ; buf_index++ ) {

		var_u32 = *((unsigned int *)&my_icar.para+buf_index) ;
		//prompt("buf_index= %04X %08X = %08X ==> %08X\r\n",buf_index,(unsigned int *)&my_icar.para+buf_index, \
			var_u32,my_icar.upgrade.base - 0x800 + buf_index*4);

		flash_prog_u16(my_icar.upgrade.base - 0x800 + buf_index*4,\
						var_u32&0xFFFF);

		flash_prog_u16(my_icar.upgrade.base - 0x800 + buf_index*4+2,\
						(var_u32>>16)&0xFFFF);
	}

	//Calc CRC

	/* Reset CRC generator */
	//CRC->CR = CRC_CR_RESET;

	for ( var_u32 = 0 ; var_u32 < (sizeof(my_icar.para)/4)-1 ; var_u32++ ) {
		//prompt("%08X = %08X\r\n",my_icar.upgrade.base-0x800+var_u32*4, (*(vu32*)(my_icar.upgrade.base-0x800+var_u32*4)));
		CRC->DR = (*(vu32*)(my_icar.upgrade.base-0x800+var_u32*4));
	}
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_CRC,(CRC->DR)&0xFFFF);
	flash_prog_u16(my_icar.upgrade.base - 0x800 + PARA_CRC+2,(CRC->DR>>16)&0xFFFF);

/*
	//for verify RAM:
	for ( var_u32 = 0 ; var_u32 < (sizeof(my_icar.para)/4)-1 ; var_u32++ ) {
		prompt("var_u32= %04X %08X = %08X\r\n",var_u32,(unsigned int *)&my_icar.para+var_u32, \
			*((unsigned int *)&my_icar.para+var_u32));
	}

	//for verify Flash:
	for ( var_u32 = 0 ; var_u32 < (sizeof(my_icar.para)/4) ; var_u32++ ) {
		prompt("%08X = %08X\r\n",my_icar.upgrade.base-0x800+var_u32*4, (*(vu32*)(my_icar.upgrade.base-0x800+var_u32*4)));
	}
*/
	return ERR_UPDATE_SUCCESSFUL;
}
