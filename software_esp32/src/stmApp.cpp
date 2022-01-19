/**HEADER*******************************************************************
  project : VdMot Controller

  author : SurfGargano, Lenti84

  Comments:

  Version :

  Modifcations :


***************************************************************************
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE DEVELOPER OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*
**************************************************************************
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License.
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Copyright (C) 2021 Lenti84  https://github.com/Lenti84/VdMot_Controller

*END************************************************************************/




#include "globals.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include "stmApp.h"
#include "telnet.h"
#include <WiFi.h>
#include <Syslog.h>
#include <WiFiUdp.h>
#include "Logger.h"
#include "VdmNet.h"
#include "VdmSystem.h"
#include "stm32.h"
#include "Queue.h"


#define     MAX_CMD_LEN     10
#define     MAX_ARG_LEN     120

static const char* commCmds [] = 
               {APP_PRE_SETTARGETPOS,APP_PRE_GETONEWIRECNT,APP_PRE_GETONEWIREDATA,
                APP_PRE_SETONEWIRESEARCH,APP_PRE_SETALLVLVOPEN,APP_PRE_GETVLVDATA,
                APP_PRE_SETVLLEARN,APP_PRE_GETVERSION,APP_PRE_GETTARGETPOS,
                APP_PRE_SETMOTCHARS,APP_PRE_GETMOTCHARS,NULL};


CStmApp StmApp;

CStmApp::CStmApp() 
{
    stm32alive = 0;           // 0 - not alive, >0 - alive 
    settarget_check =false;
    tempIndex=0;
    checkTempsCount = 0;
    cmd_buffer = "";
    found = false;
    bufptr = buffer;
    buflen = 0;
    argcnt = 0;
    arg0ptr = arg0;
	arg1ptr = arg1;
    arg2ptr = arg2;
    arg3ptr = arg3;
    arg4ptr = arg4;
	argcnt = 0;
    tempsPrivCount=0;
}

void  CStmApp::app_setup() {
    UART_STM32.begin(115200, SERIAL_8N1, STM32_RX, STM32_TX, false, 20000UL); 

    for (uint8_t x = 0;x<ACTUATOR_COUNT;x++) {
        actuators[x].actual_position = 100;
        actuators[x].target_position = 0;
        actuators[x].meancurrent = 342;
        actuators[x].state = VLV_STATE_IDLE;
        actuators[x].temp1 = -500;
        actuators[x].temp2 = -500;
    }

    for(uint8_t x = 0; x<ACTUATOR_COUNT; x++)
    {
        target_position_mirror[x] = actuators[x].target_position;
    }
    commstate=COMM_IDLE;
    UART_DBG.println("application setup finished");
}

void CStmApp::valvesCalibration()
{
    uint8_t i=255;
    app_cmd(APP_PRE_SETVLLEARN,String(i));
}

void CStmApp::valvesAssembly()
{
    app_cmd(APP_PRE_SETALLVLVOPEN);
}

void CStmApp::scanTemps()
{
    tempsCount=0;
    for (uint8_t i=0;i<TEMP_SENSORS_COUNT;i++) {
        memset(tempsId[i].id,0x0,sizeof(tempsId[i].id));
    }
    StmApp.app_cmd(APP_PRE_SETONEWIRESEARCH);
}


void  CStmApp::app_loop() 
{
    app_check_data();
    app_comm_machine(); 
    app_alive_check();
}

bool CStmApp::checkCmdIsAvailable (String thisCmd)
{   
    uint8_t i=0;
    while (commCmds[i] != NULL)
    {
        if (memcmp((const char*) &thisCmd,commCmds[i],5)==0)
        {
            return(true);
        }
        i++;
    }
    UART_DBG.println("Command not found "+String(thisCmd));
    return (false);
}

void  CStmApp::app_cmd(String command,String args) 
{    
    if (checkCmdIsAvailable(command)) {
        UART_DBG.println("push "+String(command)+String(" ")+String(args));
        Queue.push(command+String(" ")+args);
    }
}

int8_t CStmApp::findTempID(char* ID) 
{
    for (uint8_t i=0;i<TEMP_SENSORS_COUNT;i++) 
    {
        if (strncmp(ID,VdmConfig.configFlash.tempsConfig.tempConfig[i].ID,sizeof(VdmConfig.configFlash.tempsConfig.tempConfig[i].ID))==0)
        {
           return (i);
        }
    }
    return -1;
}

void  CStmApp::app_check_data() 
{
    availcnt = UART_STM32.available(); 
    if (availcnt>0)
    {    
        for (int c = 0; c < availcnt; c++)
        {           
            *bufptr++ = (char) UART_STM32.read();
            buflen++;
        }
        if (buflen>=sizeof(buffer)) {
            buffer[sizeof(buffer)-1] = '\r';
        }
    } else return;

   
    if(buflen > 4) 
    {
        for (unsigned int c = 0; c < buflen; c++)
        {           
            if(buffer[c] == '\r') 
            {
                buffer[c] = '\0';
                found = true;

                buflen = 0;           // reset counter
                bufptr = buffer;    // reset ptr
            }
        }

    }

    if (found)
    {

        // devide buffer into command and data
		// ****************************************
		cmdptr = buffer;
        
		for(int x=0;x<6;x++){
			cmdptrend = strchr(cmdptr,' ');
			if (cmdptrend!=NULL) {
				*cmdptrend = '\0';
				if(x==0) 		strncpy(cmd,cmdptr,sizeof(cmd)-1);		// command
				else if(x==1) { 
                    memset (arg0,0x0,sizeof(arg0));
                    strncpy(arg0,cmdptr,sizeof(arg0)-1); 
                    argcnt=1;	
                } 	// 1st argument
				else if(x==2) {	
                    memset (arg1,0x0,sizeof(arg1));
                    strncpy(arg1,cmdptr,sizeof(arg1)-1); 
                    argcnt=2;	
                } 	// 2nd argument
                else if(x==3) {	
                    memset (arg2,0x0,sizeof(arg2));
                    strncpy(arg2,cmdptr,sizeof(arg2)-1); 
                    argcnt=3;	
                } 	// 3rd argument
                else if(x==4) {
                    memset (arg3,0x0,sizeof(arg3));	
                    strncpy(arg3,cmdptr,sizeof(arg3)-1); 
                    argcnt=4;	
                } 	// 4th argument
                else if(x==5) {	
                    memset (arg4,0x0,sizeof(arg4));
                    strncpy(arg4,cmdptr,sizeof(arg4)-1); 
                    argcnt=5;	
                } 	// 5th argument
				cmdptr = cmdptrend + 1;
			}
		}

        // stm32 alive packet
		if(memcmp("STMalive",cmd,8) == 0) {	
            if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {		
                syslog.log(LOG_DEBUG,"STMalive received");
            }
            stm32alive = COMM_ALIVE_CYCLE + 100;
		}

        // help
		else if(memcmp("help",cmd,4) == 0) {
			telnet_msg("help command received");
		}

		// get actual values
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		else if(memcmp(APP_PRE_GETACTUALPOS,cmd,5) == 0) {
            if(argcnt == 2) {
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG,"actual position answer "+String(arg0ptr)+" : "+String(arg1ptr));
                }
                actuators[atoi(arg0ptr)].actual_position = atoi(arg1ptr);
            }
            if (memcmp(APP_PRE_GETACTUALPOS,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

        // get mean current 
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		else if(memcmp(APP_PRE_GETMEANCURR,cmd,5) == 0) {
            if(argcnt == 2) {
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG,"mean current answer "+String(arg0ptr)+" : "+String(arg1ptr));  
                }
                actuators[atoi(arg0ptr)].meancurrent = atoi(arg1ptr);
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }
    
        // get status
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		else if(memcmp(APP_PRE_GETSTATUS,cmd,5) == 0) { 
            if(argcnt == 2) {
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG,"status answer "+String(arg0ptr)+" : "+String(arg1ptr));
                }
                actuators[atoi(arg0ptr)].state = atoi(arg1ptr);     
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

         // get set target answer
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		else if(memcmp(APP_PRE_SETTARGETPOS,cmd,5) == 0) {
            if(argcnt == 0) {
                settarget_check = true; 
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

        // get data values
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		else if(memcmp(APP_PRE_GETVLVDATA,cmd,5) == 0) {
            if(argcnt == 5) {
                actuators[atoi(arg0ptr)].actual_position = atoi(arg1ptr);
                actuators[atoi(arg0ptr)].meancurrent = atoi(arg2ptr);
                actuators[atoi(arg0ptr)].state = atoi(arg3ptr);
                actuators[atoi(arg0ptr)].temp1 = atoi(arg4ptr);

                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG, "got full vlv data packet");
                }
            } 
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

        // get onewire data answer
		// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        
        else if(memcmp(APP_PRE_GETONEWIRECNT,cmd,5) == 0) {
            if(argcnt == 1) {
                tempsPrivCount= atoi(arg0ptr); 
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG,"one wire count "+String(tempsPrivCount));
                }       
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

		else if(memcmp(APP_PRE_GETONEWIREDATA,cmd,5) == 0) {
            if(argcnt == 2) {
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG,"one wire data "+String(arg0ptr)+":"+String(arg1ptr));
                } 
                strncpy(tempsId[tempIndex].id,arg0ptr,sizeof(tempsId[tempIndex].id));

                int8_t idx=findTempID(arg0ptr);
                if (idx>=0) {
                    memset(temps[idx].id,0x0,sizeof(temps[idx].id)); 
                    strncpy(temps[idx].id,arg0ptr,sizeof(temps[idx].id));
                    temps[idx].temperature=atoi(arg1ptr)+VdmConfig.configFlash.tempsConfig.tempConfig[idx].offset;
                } 
                tempIndex++;
                if (tempIndex>=tempsPrivCount) {  // all temp sensors read
                    tempIndex=0;
                    tempsCount=tempsPrivCount;
                }
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }

        else if(memcmp(APP_PRE_GETVERSION,cmd,5) == 0) {
            if(argcnt > 0) {
                VdmSystem.stmVersion=arg0ptr; 
                VdmSystem.stmBuild=0;
            }
            if(argcnt > 1) {
                 VdmSystem.stmBuild=atoi(arg1ptr);
            }
            if (memcmp(cmd,(const void*) &cmd_buffer,5) ==0) cmd_buffer="";
        }
        cmd_buffer="";
    }
}

void  CStmApp::app_comm_machine()
{
    char sendbuffer[30];
    char valbuffer[10];

    static unsigned int cnt_alive = COMM_ALIVE_CYCLE;
    static unsigned int getindex = 0;
        
    static unsigned int timeout = 0;
    static unsigned int retry = 0;
   
    switch(commstate) {

        case COMM_IDLE: 
                commstate = COMM_ALIVE;
                break;

        case COMM_ALIVE: 
                if (cnt_alive) {
                    cnt_alive = COMM_ALIVE_CYCLE;
                    UART_STM32.println("ESPalive");
                    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                        syslog.log(LOG_INFO, "ESPalive");
                    }
                }
                else cnt_alive--;

                retry = 5;
                commstate = COMM_SENDTARGET;

                break;

        // send target position values
        case COMM_SENDTARGET:
                commstate = COMM_GETDATA;
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG, "checking valve positions");
                }
                for(unsigned int x=0; x<ACTUATOR_COUNT; x++)
                {
                    // check if target has changed
                    if(target_position_mirror[x] != actuators[x].target_position)
                    {
                        memset(sendbuffer,0x0,sizeof(sendbuffer));
                        strcat(sendbuffer, APP_PRE_SETTARGETPOS);
                        strcat(sendbuffer, " ");

                        itoa(x, valbuffer, 10);
                        strcat(sendbuffer, valbuffer);
                        strcat(sendbuffer, " ");   

                        itoa(actuators[x].target_position, valbuffer, 10);      
                        strcat(sendbuffer, valbuffer);
                        strcat(sendbuffer, " ");

                        UART_STM32.println(sendbuffer);
                        
                        // generate debug messages
                        if (vismode == VISMODE_DETAIL) {
                            UART_DBG.println(sendbuffer);
                            if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                                syslog.log(LOG_DEBUG, sendbuffer);
                            }
                            logger.print(sendbuffer, logger.DATA);
                        }

                        // break loop, next valve will be served in next cycle
                        x = ACTUATOR_COUNT;

                        timeout = 10;
                        commstate = COMM_CHECKTARGET;
                    }   
                }

                // update target position mirror - 
                for(unsigned int x = 0; x<ACTUATOR_COUNT; x++) {
                    target_position_mirror[x] = actuators[x].target_position;
                }
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG, "updated position mirror");
                }
                break;

        // check correct transmission of target value
        case COMM_CHECKTARGET:

                if(timeout>0) timeout--;
                else {
                    // answer received
                    if(settarget_check) {                        
                        commstate = COMM_GETDATA; 
                    }
                    else {
                        if(retry>0) retry--;
                        else commstate = COMM_GETDATA; 
                        commstate = COMM_SENDTARGET;
                    }
                }
                
                break;

        case COMM_GETDATA:  
                commstate =  COMM_GETONEWIRECOUNT;
                
                // walk through all valves, one per cycle
                if(getindex<ACTUATOR_COUNT-1) getindex++; else getindex=0;

                memset(sendbuffer,0x0,sizeof(sendbuffer));
                strcat(sendbuffer, APP_PRE_GETVLVDATA);
                strcat(sendbuffer, " ");

                itoa(getindex, valbuffer, 10);
                strcat(sendbuffer, valbuffer);
                strcat(sendbuffer, " ");   

                UART_STM32.println(sendbuffer);
                
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG, sendbuffer);
                }
                // generate debug messages
                if (vismode == VISMODE_DETAIL) {
                    UART_DBG.println(sendbuffer);                
                    logger.print(sendbuffer, logger.DATA);
                }     
                
                break;

        case COMM_GETONEWIRECOUNT:
                commstate = COMM_GETONEWIRE;
                if (checkTempsCount==0) {
                    memset(sendbuffer,0x0,sizeof(sendbuffer));
                    
                    strcat(sendbuffer, APP_PRE_GETONEWIRECNT);
                    strcat(sendbuffer, " ");
                    UART_STM32.println(sendbuffer);
                    checkTempsCount=20;
                }    
                checkTempsCount--;
                break;

        case COMM_GETONEWIRE:
                commstate = COMM_HANDLEQUEUE;
                if (tempsPrivCount>0) {
                    memset(sendbuffer,0x0,sizeof(sendbuffer));
                    
                    strcat(sendbuffer, APP_PRE_GETONEWIREDATA);
                    strcat(sendbuffer, " ");

                    itoa(tempIndex, valbuffer, 10);
                    strcat(sendbuffer, valbuffer);
                    strcat(sendbuffer, " ");   

                    UART_STM32.println(sendbuffer);
                    }
                break;
        case COMM_HANDLEQUEUE:
                commstate = COMM_IDLE;
                if ((cmd_buffer=="") && (Queue.available()>0)) {
                    memset(&cmd_buffer,0x0,sizeof(cmd_buffer));
                    cmd_buffer=Queue.pop()+String(" ");
                    UART_DBG.println("pop "+String(cmd_buffer));
                    UART_STM32.println(cmd_buffer);
                }
                break;
        
        default:    
                commstate = COMM_IDLE;
                break;
    }
}

void  CStmApp::app_alive_check() 
{
    static uint8_t oldalivestate;

    if(stm32alive) {
        stm32alive--;
        if (oldalivestate == 0) {
            if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_ON) {
                syslog.log(LOG_DEBUG, "connection to STM32 established");
            }
            // generate debug messages
            if (vismode > VISMODE_OFF) {
                logger.print("connection to STM32 established");
            }
        }
        oldalivestate = 1;
    }
    else {
        // generate debug messages
        if (oldalivestate == 1) {
            if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_ON) {
                syslog.log(LOG_DEBUG, "connection to STM32 lost");
            }
            if (vismode > VISMODE_OFF) {
                logger.print("connection to STM32 lost");
            }
        }
        oldalivestate = 0;
    }
}

