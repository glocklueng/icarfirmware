/**
  ******************************************************************************
  * SVN revision information:
  * @file    $URL$ 
  * @version $Rev$
  * @author  $Author$
  * @date    $Date$
  * @brief   This is realtime clock driver in STM32
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DRV_RTC_H
#define __DRV_RTC_H

/* Exported macro ------------------------------------------------------------*/

/* Includes ------------------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
struct RTC_STATUS {
	//unsigned char update_count ;
	u32 update_timer ;//re-update if > RTC_UPDATE_PERIOD
	u16 prescaler ;  //++ if rtc slow, -- if rtc fast
};

struct DATE_TIME
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
};

/* Exported constants --------------------------------------------------------*/

/* Exported functions ------------------------------------------------------- */
void rtc_init(void);
void RTC_show_time( unsigned int );
void RTC_update_calibrate( unsigned char *, unsigned char * ) ;
//void seconds_to_datetime (unsigned int counts, struct DATE_TIME *datetime);
#endif /* __DRV_RTC_H */
