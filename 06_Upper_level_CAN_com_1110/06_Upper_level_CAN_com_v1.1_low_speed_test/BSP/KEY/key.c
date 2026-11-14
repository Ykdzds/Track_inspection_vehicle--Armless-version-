#include "key.h"

KEY keys[3] = {0};

void key_scan(void)
{
//    static uint8_t key_times = 0;
//    
//    key_times ++;
//    if(key_times >= 10)
//    {
//        key_times = 0;
        
        keys[0].key_sta = HAL_GPIO_ReadPin(KEY1_GPIO_Port,KEY1_Pin);
        keys[1].key_sta = HAL_GPIO_ReadPin(KEY2_GPIO_Port,KEY2_Pin);
        keys[2].key_sta = HAL_GPIO_ReadPin(KEY3_GPIO_Port,KEY3_Pin);
        
        for(int i = 0;i < 3;i ++)
        {
            switch(keys[i].key_judge)
            {
                case 0:
                {
                    if(keys[i].key_sta == RESET)
                    {
                        keys[i].key_judge = 1;
                        keys[i].key_times = 0;
                        keys[i].affirm_longflag = 0;
                    }
                }
                break;
                case 1:
                {
                    if(keys[i].key_sta == RESET)keys[i].key_judge = 2;
                    else keys[i].key_judge = 0;
                }
                break;
                case 2:
                {
                    if(keys[i].key_sta == SET)
                    {
                        if(keys[i].key_times <= 200)keys[i].key_flag = 1;
                        keys[i].key_judge = 0;
                    }
                    else 
                    {
                        keys[i].key_times ++;
                        if(keys[i].key_times > 200)keys[i].key_times = 203;
                        if(keys[i].key_times > 200 && keys[i].affirm_longflag == 0)
                        {
                            keys[i].long_flag = 1;
                            keys[i].affirm_longflag = 1;
                        }
                    }
                }
                break;
                default:break;
            }
        }        
//    }
}

void key_handle(void)
{
    if(keys[0].key_flag == 1)
    {
        keys[0].key_flag = 0;
      
    }
    else if(keys[1].key_flag == 1)
    {
        keys[1].key_flag = 0;
      
    }
    else if(keys[2].key_flag == 1)
    {
        keys[2].key_flag = 0;
      
    }
    
    if(keys[0].long_flag == 1)
    {
        keys[0].long_flag = 0;

    }
    else if(keys[1].long_flag == 1)
    {
        keys[1].long_flag = 0;      /*PB1 has no long press function*/
    }
    else if(keys[2].long_flag == 1)
    {
        keys[2].long_flag = 0;

    }
}
