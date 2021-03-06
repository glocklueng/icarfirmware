/**
  ******************************************************************************
  * SVN revision information:
  * @file    $URL$ 
  * @version $Rev$
  * @author  $Author$
  * @date    $Date$
  * @brief   This is GSM module: MG323 driver
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DRV_MG323_H
#define __DRV_MG323_H

/* Exported macro ------------------------------------------------------------*/

/* Includes ------------------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported functions ------------------------------------------------------- */
unsigned char cmpmem(unsigned char *, unsigned char *,unsigned char );
unsigned char gsm_power_on( void ) ;
unsigned char gsm_check_gprs( void );
bool gprs_disconnect( void );
bool gsm_pwr_off( void );
void save_2g_err( unsigned char, GPRS_REASON);
unsigned char check_gsm_CSQ( void );
unsigned char check_tcp_status( void );
bool get_respond( unsigned char* rec_str);
bool gsm_send_tcp( unsigned char *send_buf, unsigned int dat_len );
bool gsm_ask_tcp( unsigned int dat_len ) ;
#endif /* __DRV_MG323_H */
