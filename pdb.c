/*
 * pdb.c - PIC debugger
 * a simple debugger to be used with the open programmer
 * Copyright (C) 2010 Alberto Maccioni
 * for detailed info see:
 * http://openprog.altervista.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 * or see <http://www.gnu.org/licenses/>
 */


#if !defined _WIN32 && !defined __CYGWIN__
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <asm/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hiddev.h>
#include <linux/input.h>
#else
#include <windows.h>
#include <setupapi.h>
#include <ddk/hidusage.h>
#include <ddk/hidpi.h>
#endif

#include <sys/timeb.h>
#include <wchar.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include "strings.h"
#include "instructions.h"

#define COL 16
#define VERSION "0.1"

#if !defined _WIN32 && !defined __CYGWIN__
    #define write() ioctl(fd, HIDIOCSUSAGES, &ref_multi_u); ioctl(fd,HIDIOCSREPORT, &rep_info_u);
    #define read() ioctl(fd, HIDIOCGUSAGES, &ref_multi_i); ioctl(fd,HIDIOCGREPORT, &rep_info_i);
    #define bufferU ref_multi_u.values
    #define bufferI ref_multi_i.values

#include <sys/select.h>
int kbhit()
{
	struct timeval tv;
	fd_set read_fd;
	tv.tv_sec=0;
	tv.tv_usec=0;
	FD_ZERO(&read_fd);
	FD_SET(0,&read_fd);
	if(select(1, &read_fd, NULL, NULL, &tv) == -1) return 0;
	if(FD_ISSET(0,&read_fd)) return 1;
	return 0;
}

#else
	#define write()	Result=WriteFile(WriteHandle,bufferU,DIMBUF,&BytesWritten,NULL);
	#define read()	Result = ReadFile(ReadHandle,bufferI,DIMBUF,&NumberOfBytesRead,(LPOVERLAPPED) &HIDOverlapped);\
					Result = WaitForSingleObject(hEventObject,10);\
					ResetEvent(hEventObject);\
					if(Result!=WAIT_OBJECT_0){\
						/*printf("timeout\n");*/\
					}
#endif

//#define NOP		0	;nop
#define VER 	1	//;version
#define STEP 	2	//;step
#define GO 		3	//;go
#define RREG 	4	//;read register
#define WREG 	5	//;write register
#define EEADR 0x10D
#define EEADRH 0x10F
#define EEDATA 0x10C
#define EEDATH 0x10E
#define EECON1 0x18C
#define EECON2 0x18D
#define w_temp 0x6B
#define status_temp 0x6C
#define pclath_temp 0x6D
#define fsr_temp 0x6E



typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;

#if !defined _WIN32 && !defined __CYGWIN__
DWORD GetTickCount();
#endif
void msDelay(double delay);
void WriteLogIO();
int FindDevice();
int ReadRegister(int addr);
int ReadProgMem(int addr);
int ReadDataMem(int addr);
void WriteRegister(int addr,int data);
void Halt();
void ShowVariables();
void ShowContext();
char* decodeCmd(int cmd,char *str,int addrH);
int ReadRegisterN(int addr,int n,int* buf);
char* getVar(int addr,char *var);

int saveLog=0,MinDly=1;
int showVar=1,showCtx=1,running=0,Tck=30;
int FWVersion=0;
double Tcom;
FILE* LogFile=0;
char LogFileName[256]="pdb_log.txt";
char loadfile[256]="",savefile[256]="";
int size=0,sizeEE=0,sizeCONFIG=0;
unsigned char *memCODE,*memEE,memID[8],memCONFIG[34];
int vid=0x04D8,pid=0x0100,info=0;
struct var{	char* name;	int display;} variables[0x200];
#if !defined _WIN32 && !defined __CYGWIN__
int fd = -1;
struct hiddev_report_info rep_info_i,rep_info_u;
struct hiddev_usage_ref_multi ref_multi_i,ref_multi_u;
int DIMBUF=64;
char path[256]="/dev/usb/hiddev0";
#else
unsigned char bufferU[128],bufferI[128]; 
DWORD NumberOfBytesRead,BytesWritten;
ULONG Result;
HANDLE WriteHandle,ReadHandle;
OVERLAPPED HIDOverlapped;
HANDLE hEventObject;
int DIMBUF=65;
#endif

// gestione Wtemp STATUS ecc

int main (int argc, char **argv) {
	int ver=0,c=0,i,j,z,quiet=0,reset=1,freeze=0;
	int break_addr,print_addr;
	unsigned char tmpbuf[128];
	char loadpath[512]="";
	FILE* LoadFile=0;
	opterr = 0;
	int option_index = 0;
	struct option long_options[] =
	{
		{"h",             no_argument,             0, 'h'},
		{"help",          no_argument,             0, 'h'},
		{"info",          no_argument,           &info, 1},
		{"i",             no_argument,           &info, 1},
		{"load",          required_argument,       0, 'L'},
		{"log",           optional_argument,       0, 'l'},
#if !defined _WIN32 && !defined __CYGWIN__
		{"p",             required_argument,       0, 'p'},
		{"path",          required_argument,       0, 'p'},
#endif
		{"pid",           required_argument,       0, 'P'},
		{"tck",             required_argument,       0, 't'},
		{"version",       no_argument,            &ver, 1},
		{"v",             no_argument,            &ver, 1},
		{"vid",           required_argument,       0, 'V'},
		{0, 0, 0, 0}
	};
	while ((c = getopt_long_only (argc, argv, "",long_options,&option_index)) != -1)
		switch (c)
		{
			case 'h':	//help
				printf("pdb: an In Circuit Debugger for PIC microcontrollers\
	\noptions:\
	\n-h, help                    help\
	\n-i, info                    info about programmer\
	\n-load <file>                load variable definitions\
	\n-l, log [=file]             save log\
	\n-p, path <path>             programmer path [/dev/usb/hiddev0]\
	\n-pid <pid>                  programmer pid [0x100]\
	\n-tck <t>                    use clock period of 2*<t> for communication with target\
	\n-version                    display version\
	\n-vid <vid>                  programmer vid [0x4D8]\
	\n");
				return 1 ;
				break;
			case 'l':	//save Log
				saveLog=1;
				if(optarg) strncpy(LogFileName,optarg,sizeof(LogFileName));
				break;
			case 'L':	//load variables
				strncpy(loadpath,optarg,sizeof(loadpath)-1);
				break;
			case 'r':	//USB HID report size
				DIMBUF = atoi(optarg);
				break;
#if !defined _WIN32 && !defined __CYGWIN__
			case 'p':	//hiddev path
				strncpy(path,optarg,sizeof(path)-1);
				break;
#endif
			case 'P':	//pid
				sscanf(optarg, "%x", &pid);
				break;
			case 't':	//vid
				sscanf(optarg, "%d", &Tck);
				break;
			case 'V':	//vid
				sscanf(optarg, "%x", &vid);
				break;
			case '?':
				fprintf (stderr,"error in the options\n");
				return 1;
			default:

				break;
		}

	for(j=0,i = optind; i < argc&&i<128; i++,j++) sscanf(argv[i], "%x", &tmpbuf[j]);
	for(;j<128;j++) tmpbuf[j]=0;
	Tcom=0.002*Tck*18+0.03; //communication time for a 16 bit tranfer (ms)
	for(i=0;i<0x200;i++){//clear variable list
		variables[i].name=0;
		variables[i].display=0;
	}
	variables[0].name="INDF";
	variables[1].name="TMR0";
	variables[2].name="PCL";
	variables[3].name="STATUS";
	variables[4].name="FSR";
	variables[5].name="PORTA";
	variables[6].name="PORTB";
	variables[7].name="PORTC";
	variables[8].name="PORTD";
	variables[9].name="PORTE";
	variables[10].name="PCLATH";
	variables[11].name="INTCON";
	variables[12].name="PIR1";
	variables[13].name="PIR2";
	variables[14].name="TMR1L";
	variables[15].name="TMR1H";
	variables[16].name="T1CON";
	variables[17].name="TMR2";
	variables[18].name="T2CON";
	variables[19].name="SSPBUF";
	variables[20].name="SSPCON";
	variables[21].name="CCPR1L";
	variables[22].name="CCPR1H";
	variables[23].name="CCP1CON";
	variables[24].name="RCSTA";
	variables[25].name="TXREG";
	variables[26].name="RCREG";
	variables[27].name="CCPR2L";
	variables[28].name="CCPR2H";
	variables[29].name="CCP2CON";
	variables[30].name="ADRESH";
	variables[31].name="ADCON0";
	variables[0x6B].name="DEBUG_VAR1";
	variables[0x6C].name="DEBUG_VAR2";
	variables[0x6D].name="DEBUG_VAR3";
	variables[0x6E].name="DEBUG_VAR4";
	variables[0x6F].name="DEBUG_VAR5";
	variables[0x70].name="DEBUG_VAR6";
	variables[0x71].name="DEBUG_VAR7";
	variables[0x72].name="DEBUG_VAR8";
	variables[0x80].name="INDF";
	variables[0x81].name="OPTION_REG";
	variables[0x82].name="PCL";
	variables[0x83].name="STATUS";
	variables[0x84].name="FSR";
	variables[0x85].name="TRISA";
	variables[0x86].name="TRISB";
	variables[0x87].name="TRISC";
	variables[0x88].name="TRISD";
	variables[0x89].name="TRISE";
	variables[0x8A].name="PCLATH";
	variables[0x8B].name="INTCON";
	variables[0x8C].name="PIE1";
	variables[0x8D].name="PIE2";
	variables[0x8E].name="PCON";
	variables[0x91].name="SSPCON2";
	variables[0x92].name="PR2";
	variables[0x93].name="SSPADD";
	variables[0x94].name="SSPSTAT";
	variables[0x98].name="TXSTA";
	variables[0x99].name="SPBRG";
	variables[0x9E].name="ADRESL";
	variables[0x9F].name="ADCON1";
	variables[0x100].name="INDF";
	variables[0x101].name="TMR0";
	variables[0x102].name="PCL";
	variables[0x103].name="STATUS";
	variables[0x104].name="FSR";
	variables[0x106].name="PORTB";
	variables[0x10A].name="PCLATH";
	variables[0x10B].name="INTCON";
	variables[0x10C].name="EEDATA";
	variables[0x10D].name="EEADR";
	variables[0x10E].name="EEDATH";
	variables[0x10F].name="EEADRH";
	variables[0x180].name="INDF";
	variables[0x181].name="OPTION_REG";
	variables[0x182].name="PCL";
	variables[0x183].name="STATUS";
	variables[0x184].name="FSR";
	variables[0x186].name="TRISB";
	variables[0x18A].name="PCLATH";
	variables[0x18B].name="INTCON";
	variables[0x18C].name="EECON1";
	variables[0x18D].name="EECON2";
	if(loadpath[0]){
		LoadFile=fopen(loadpath,"r");
		if(!LoadFile) return -1;
		char line[256],var[128];
		int addr;
		for(;fgets(line,256,LoadFile);){
			addr=-1;
			if(sscanf(line,"%s %x",var,&addr)==2&&addr>=0&&addr<0x200){
				if(variables[addr].name) free(variables[addr].name);
				variables[addr].name=malloc(strlen(var)+1);
				strcpy(variables[addr].name,var);
			}
		}
	}		
	if (ver){
		printf("pdb v%s an In Circuit Debugger for PIC microcontrollers\
\nCopyright (C) Alberto Maccioni 2010\
\n	For detailed info see http://openprog.altervista.org/\
\nThis program is free software; you can redistribute it and/or modify it under \
the terms of the GNU General Public License as published by the Free Software \
Foundation; either version 2 of the License, or (at your option) any later version.\
			\n",VERSION);
		return 0;
	}
	printf("pdb, an In Circuit Debugger for PIC microcontrollers\n");
	if(FindDevice()<0);// exit(1);
#if !defined _WIN32 && !defined __CYGWIN__
	if(info){
		struct hiddev_devinfo device_info;
		ioctl(fd, HIDIOCGDEVINFO, &device_info);
		printf("",device_info.vendor, device_info.product, device_info.version);
		printf("",device_info.busnum, device_info.devnum, device_info.ifnum);
		char name[256];
		strcpy(name,"Unknown");
		if(ioctl(fd, HIDIOCGNAME(sizeof(name)), name) < 0) perror("evdev ioctl");
		printf("The device on %s says its name is %s\n", path, name);
		return 0;
	}
#endif
	DWORD t0,t;
	t=t0=GetTickCount();
	bufferU[0]=0;
	j=1;
	bufferU[j++]=PROG_RST;
	bufferU[j++]=SET_PARAMETER;
	bufferU[j++]=SET_T1T2;
	bufferU[j++]=Tck;					//T1=XXu
	bufferU[j++]=100;					//T2=100u
	bufferU[j++]=VREG_DIS;			//disable HV regulator
	bufferU[j++]=EN_VPP_VCC;		// reset target
	bufferU[j++]=0x0;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;
	bufferU[j++]=EN_VPP_VCC;		// power-up
	bufferU[j++]=0x1;
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(2);
	read();
	FWVersion=(bufferI[2]<<16)+(bufferI[3]<<8)+bufferI[4];
	if(FWVersion<0x706){ printf("This firmware is too old, 0.7.6 is required"); return -1;}
	if(saveLog){
		LogFile=fopen(LogFileName,"w");
		if(!LogFile) return -1;
		fprintf(LogFile,"pdb version %s\n",VERSION);
		fprintf(LogFile,"Firmware version %d.%d.%d\n",FWVersion>>16,(FWVersion>>8)&0xFF,FWVersion&0xFF);
		fprintf(LogFile,"Tck=%d Tcom=%.3f\n",Tck,Tcom);
		struct tm * timeinfo;
		time_t rawtime;
		time( &rawtime );                /* Get time as long integer. */
		timeinfo = localtime( &rawtime ); /* Convert to local time. */
		fprintf(LogFile,"%s\n", asctime (timeinfo) );
	}
	if(saveLog)WriteLogIO();
	char command[128],last_cmd[128];
	command[0]=last_cmd[0]=0;
	break_addr=0x1FFF;
	char x[]="--\\\\||//";
	for(;strcmp(command,"quit")&&strcmp(command,"q");){
		//running cycle: wait for break or command
		for(i=0;running&&!kbhit();msDelay(20),i++){
			if(i>7) i=0;
			j=1;
			bufferU[j++]=SET_CK_D;
			bufferU[j++]=0x0;		//D=0
			bufferU[j++]=SET_CK_D;
			bufferU[j++]=0x2;		//set D as input
			bufferU[j++]=READ_PINS;
			bufferU[j++]=FLUSH;
			for(;j<DIMBUF;j++) bufferU[j]=0x0;
			write();
			msDelay(2);
			read();
			if(saveLog)WriteLogIO();
			for(z=0;z<DIMBUF-1&&bufferI[z]!=READ_PINS;z++);
			if(bufferI[z+1]&1){
				running=0;	//if D=1 the ICD routine is executing
				printf("\rexecution stopped\n");
				ShowVariables();
				if(showCtx)ShowContext();
			}
			printf("\r%c   ",x[i]);fflush(stdout);
		}
		printf("\r        \r>");
		gets(command);
		if(strlen(command)==0) strcpy(command,last_cmd);
		else strcpy(last_cmd,command);
//******************* break ********************************
		if(strstr(command,"break")){
			if(sscanf(command,"break %x",&break_addr)==1){
				if(running) Halt();
				break_addr&=0x1FFF;
				printf("break at address 0x%04X\n",break_addr);
			}
		}
//******************* continue ********************************
		else if(!strcmp(command,"c")||!strcmp(command,"continue")){
			if(!running){	//continue if halted
				j=1;
				//set breakpoint and freeze
				break_addr&=0x1FFF;
				bufferU[j++]=TX16;
				bufferU[j++]=0x2;
				bufferU[j++]=WREG;		//write register
				bufferU[j++]=(break_addr>>8)+(freeze?0x40:0);
				bufferU[j++]=0x1;
				bufferU[j++]=0x8E;
				bufferU[j++]=TX16;
				bufferU[j++]=0x2;
				bufferU[j++]=WREG;		//write register
				bufferU[j++]=break_addr&0xFF;
				bufferU[j++]=0x1;
				bufferU[j++]=0x8F;
				bufferU[j++]=TX16;
				bufferU[j++]=0x1;
				bufferU[j++]=GO;		//GO
				bufferU[j++]=0;
				bufferU[j++]=SET_CK_D;
				bufferU[j++]=0x2;		//set D as input
				bufferU[j++]=FLUSH;
				for(;j<DIMBUF;j++) bufferU[j]=0x0;
				write();
				msDelay(1+5*Tcom);
				read();
				if(saveLog)WriteLogIO();
				printf("running\n");		
				running=1;
			}
			else printf("already running\n");
		}
//******************* define ********************************
		if(strstr(command,"define")){
			int addr,i;
			char var[32];
			if(!strcmp(command,"define list")){
				printf("list of variables:\n");
				for(i=0;i<0x200;i++) if(variables[i].name) printf("0x%03X: %s\n",i,variables[i].name);
			}
			else if(!strcmp(command,"define rm all")){
				for(i=0;i<0x200;i++){
					if(variables[i].name){
						free(variables[i].name);
						variables[i].name=0;
						variables[i].display=0;
					}
				}
				printf("removed all variables\n");
			}
			else if(sscanf(command,"define rm %x",&i)==1&&variables[i].name){
				printf("%s (0x%X) removed from variable list\n",variables[i].name,i);
				free(variables[i].name);
				variables[i].name=0;
				variables[i].display=0;
			}
			else if(sscanf(command,"define rm %s",var)==1){
				for(i=0;i<0x200;i++){
					if(variables[i].name&&!strcmp(var,variables[i].name)){
						free(variables[i].name);
						variables[i].name=0;
						variables[i].display=0;
						printf("%s (0x%X) removed from variable list\n",var,i);
					}
				}
			}
			else if(sscanf(command,"define %s %x",var,&addr)==2){
				addr&=0x1FF;
				if(variables[addr].name) free(variables[addr].name);
				variables[addr].name=malloc(strlen(var)+1);
				strcpy(variables[addr].name,var);
				printf("%s (0x%X) added to variable list\n",var,addr);
			}
		}
//******************* display (each time the program stops) ********************************
		if(strstr(command,"display")){
			int addr,i;
			char var[32];
			if(!strcmp(command,"display")&&!running){
				ShowVariables();
				if(showCtx)ShowContext();
			}
			else if(!strcmp(command,"display off")) showVar=0;
			else if(!strcmp(command,"display on")) showVar=1;
			else if(!strcmp(command,"display list")){
				printf("list of variables on display:\n");
				for(i=0;i<0x200;i++) if(variables[i].display&&variables[i].name) printf("0x%03X: %s\n",i,variables[i].name);
			}
			else if(!strcmp(command,"display rm all")){
				for(i=0;i<0x200;i++){
					if(variables[i].display)variables[i].display=0;
				}
				printf("removed all variables:\n");
			}
			else if(sscanf(command,"display rm %x",&i)==1&&variables[i].display&&variables[i].name){
				printf("variable %s (0x%X) removed from display list\n",variables[i].name,i);
				variables[i].display=0;
			}
			else if(sscanf(command,"display rm %s",var)==1){
				for(i=0;i<0x200;i++){
					if(variables[i].display&&variables[i].name&&!strcmp(var,variables[i].name)){
						variables[i].display=0;
						printf("variable %s (0x%X) removed from display list\n",var,i);
					}
				}
			}
			else if(sscanf(command,"display %s",var)==1){
				if(!strcmp("W",var));
				else if(!strcmp("STATUS",var));
				else if(!strcmp("FSR",var));
				else if(!strcmp("PCLATH",var));
				else for(i=0;i<0x200;i++){
					if(variables[i].name&&!strcmp(var,variables[i].name)){
						variables[i].display=1;
						printf("variable %s (0x%X) added to display list\n",var,i);
						i=0x1000;
					}
				}
			}
		}
//******************* freeze ********************************
		else if(strstr(command,"freeze")){
			char option[32];
			int ick;
			if(sscanf(command,"freeze %s",option)==1){
				if(running) Halt();
				if(!strcmp(option,"on")) freeze=1;
				if(!strcmp(option,"off")) freeze=0;
				WriteRegister(0x18E,(break_addr>>8)+(freeze?0x40:0));
			}
			printf("peripheral freeze is %s\n",freeze?"on":"off");
		}
//******************* halt1 ********************************
		else if(!strcmp(command,"h0")||!strcmp(command,"halt0")){
			int addr,z,data;
			 Halt();
		}
//******************* halt ********************************
		else if(!strcmp(command,"h")||!strcmp(command,"halt")){
			int addr,z,data;
			if(running) Halt();//
			else printf("already halted\n");
			ShowVariables();
			if(showCtx)ShowContext();
		}
//******************* help ********************************
		else if(!strcmp(command,"help")){
			printf("commands:\
		\n help                command help\
		\n break <addr>        set breakpoint at address <addr>\
		\n c[ontinue]          continue execution after halt\
		\n define <var> <a>    define variable named <var> at address <addr>\
		\n define rm <a>       remove definition for variable at address <addr>\
		\n define rm <var>     remove definition for variable named <var>\
		\n define rm all       remove all variable definitions\
		\n define list         list variables\
		\n display             if halted read variables in display list\
		\n display <var>       add variable named <var> to display list\
		\n display rm <a>      remove variable at address <addr> from the display list\
		\n display rm <var>    remove variable named <var> from the display list\
		\n display rm all      remove all variables from the display list\
		\n display list        list variables in display list\
		\n display {on off}    turn on-off auto display\
		\n freeze [on,off]     freeze peripherals\
		\n h[alt]              halt execution\
		\n list [a]            display program code in the vicinity of current \
		\n                     instruction or address [a]\
		\n n[ext]              step over calls\
		\n print <addr>        print variable at address <addr>\
		\n print <var>         print variable <var>\
		\n print bank <b>      print registers in bank <b>\
		\n print p <addr>      print program memory at address <addr>\
		\n print ee <addr>     print eeprom memory at address <addr>\
		\n print ee            print all eeprom memory\
		\n q[uit]              quit program\
		\n r[un]               remove MCLR and run\
		\n set <a> <d>         write <d> at address <a>\
		\n set <var> <d>       write <d> to variable <var>\
		\n s[tep] [n]          single step [n times]\
		\n version             read debugger version\
		\n");
		}
//******************* list ********************************
		else if(strstr(command,"list")){
			int addr,addr0,addr1,data,pc=1;
			char cmd[32];
			if(sscanf(command,"list %x",&addr)!=1)
				addr=((ReadRegister(0x18E)&0x1F)<<8)+ReadRegister(0x18F);
			else pc=0;
			if(running) Halt();
			addr0=addr-4<0?0:addr-4;
			addr1=addr+6>0x1FFF?0x1FFF:addr+6;
			for(i=addr0;i<addr1;i++){
				data=ReadProgMem(i);		
				printf("0x%04X: %s (0x%04X)\t%s\n",i,decodeCmd(data,cmd,0x1000),data,i==addr&&pc?"<-PC":"");
			}
		}
//******************* next ********************************
		else if(!strcmp(command,"n")||!strcmp(command,"next")){
			int addr,break_old,data;
			if(running) Halt();
			addr=((ReadRegister(0x18E)&0x1F)<<8)+ReadRegister(0x18F);
			data=ReadProgMem(addr);
			if((data>>11)==4){	//if call
				break_old=break_addr;
				break_addr=addr+1;
				j=1;
				//set breakpoint and freeze
				break_addr&=0x1FFF;
				bufferU[j++]=TX16;
				bufferU[j++]=0x2;
				bufferU[j++]=WREG;		//write register
				bufferU[j++]=(break_addr>>8)+(freeze?0x40:0);
				bufferU[j++]=0x1;
				bufferU[j++]=0x8E;
				bufferU[j++]=TX16;
				bufferU[j++]=0x2;
				bufferU[j++]=WREG;		//write register
				bufferU[j++]=break_addr&0xFF;
				bufferU[j++]=0x1;
				bufferU[j++]=0x8F;
				bufferU[j++]=TX16;
				bufferU[j++]=0x1;
				bufferU[j++]=GO;		//GO
				bufferU[j++]=0;
				bufferU[j++]=SET_CK_D;
				bufferU[j++]=0x2;		//set D as input
				bufferU[j++]=FLUSH;
				for(;j<DIMBUF;j++) bufferU[j]=0x0;
				write();
				msDelay(1+5*Tcom);
				read();
				if(saveLog)WriteLogIO();
				running=1;
				printf("next\n");
			}
			else{		//normal step
				j=1;
				bufferU[j++]=TX16;
				bufferU[j++]=0x1;
				bufferU[j++]=STEP;		//single step
				bufferU[j++]=0;
				bufferU[j++]=SET_CK_D;
				bufferU[j++]=0x2;		//set D as input
				bufferU[j++]=FLUSH;
				for(;j<DIMBUF;j++) bufferU[j]=0x0;
				write();
				msDelay(1+Tcom);
				read();
				if(saveLog)WriteLogIO();
				printf("next\n");
				ShowVariables();
				if(showCtx)ShowContext();
			}
		}
//******************* print ********************************
		if(strstr(command,"print")){
			int bank,i,addr,data;
			char var[128];
			if(strstr(command,"print p")){	//program memory
				int addr;
				if(sscanf(command,"print p %x",&addr)==1){
					addr&=0x1FFF;
					if(running) Halt();
					data=ReadProgMem(addr);
					printf("0x%04X: %s (0x%04X)\n",addr,decodeCmd(data,var,0x1000),data);
				}
			}
			else if(!strcmp(command,"print ee")){	//eeprom
				char text[32];
				text[16]=0;
				if(running) Halt();
				printf("eeprom:");
				for(i=0;i<0x100;i++){
					data=ReadDataMem(i);
					if(i%16==0) printf("\n0x%02X: ",i);
					printf("%02X ",data);
					text[i&0xF]=isprint(data)?data:'.';
					if(i%16==15) printf(text);
				}
				printf("\n");
			}
			else if(sscanf(command,"print ee %x",&addr)==1){
				addr&=0xFF;
				if(running) Halt();
				data=ReadDataMem(addr);
				printf("eeprom memory at 0x%02X=0x%02X (%c)\n",addr,data,isprint(data)?data:'.');
			}
			else if(sscanf(command,"print bank %x",&bank)==1){
				int b[256];
				bank&=0x1FF;
				if(bank>3) bank/=0x80;
				if(running) Halt();
				printf("bank %d:",bank);
				ReadRegisterN(bank*0x80,256,b);
				for(i=0;i<256;i++){
					if(i%16==0) printf("\n0x%03X:",i+bank*0x80);
					printf("%02X",b[i]);
				}
				printf("\n");
			}
			else if(sscanf(command,"print 0x%x",&print_addr)==1){
				print_addr&=0x1FF;
				if(running) Halt();
				printf("register at 0x%03X=0x%02X\n",print_addr,ReadRegister(print_addr));
			}
			else if(sscanf(command,"print %s",var)==1){
				if(!strcmp("W",var)) printf("W = 0x%02X\n",ReadRegister(w_temp));
				else if(!strcmp("STATUS",var)) printf("0x003: STATUS = 0x%02X\n",ReadRegister(status_temp));
				else if(!strcmp("FSR",var)) printf("0x004: FSR = 0x%02X\n",ReadRegister(fsr_temp));
				else if(!strcmp("PCLATH",var)) printf("0x00A: PCLATH = 0x%02X\n",ReadRegister(pclath_temp));
				else for(i=0;i<0x200;i++){
					if(variables[i].name&&!strcmp(variables[i].name,var)){
						printf("0x%03X: %s = 0x%02X\n",i,variables[i].name,ReadRegister(i));
						i=0xFFF;
					}
				}
			}
		}
//******************* run ********************************
		else if(!strcmp(command,"r")||!strcmp(command,"run")){
			if(reset){	//run if reset
				j=1;
				bufferU[j++]=SET_CK_D;
				bufferU[j++]=0x2;
				bufferU[j++]=EN_VPP_VCC;		//MCLR=H
				bufferU[j++]=0x5;
				bufferU[j++]=FLUSH;
				for(;j<DIMBUF;j++) bufferU[j]=0x0;
				write();
				msDelay(1);
				read();
				if(saveLog)WriteLogIO();
				if(!quiet)printf("running\n");
				running=1;
				reset=0;
			}
			else printf("already not under reset (try continue)\n");
		}
//******************* set ********************************
		else if(strstr(command,"set")){
			int addr,data,i;
			char var[128];
			if(sscanf(command,"set %s %x",var,&data)==2){
				data&=0xFF;
				i=0x1000;
				if(!strcmp("W",var)) addr=w_temp;
				else if(!strcmp("STATUS",var)) addr=status_temp;
				else if(!strcmp("FSR",var)) addr=fsr_temp;
				else if(!strcmp("PCLATH",var)) addr=pclath_temp;
				else for(i=0;i<0x200;i++){
					if(variables[i].name&&!strcmp(var,variables[i].name)){
						addr=i;
						i=0xFFF;
					}
				}
				if(i==0x1000){
					if(running) Halt();
					WriteRegister(addr,data);
					printf("0x%02X written to %s\n",data,var);
				}
				else printf("symbol %s is unknown\n",var);
			}
			if(sscanf(command,"set %x %x",&addr,&data)==2){
				addr&=0x1FF;
				data&=0xFF;
				if(running) Halt();
				WriteRegister(addr,data);
				printf("0x%02X written at address 0x%03X\n",data,addr);
			}
		}
//******************* step ********************************
		else if(!strcmp(command,"s")||!strcmp(command,"step")||strstr(command,"step")){
			int i,n=1;
			sscanf(command,"step %d",&n);
			if(running) Halt();
			for(i=0;i<n;i++){
				j=1;
				bufferU[j++]=TX16;
				bufferU[j++]=0x1;
				bufferU[j++]=STEP;		//single step
				bufferU[j++]=0;
				bufferU[j++]=SET_CK_D;
				bufferU[j++]=0x2;		//set D as input
				bufferU[j++]=FLUSH;
				for(;j<DIMBUF;j++) bufferU[j]=0x0;
				write();
				msDelay(1+Tcom);
				read();
				if(saveLog)WriteLogIO();
			}
			if(n>1)printf("step %d\n",n);
			else printf("step\n");
			ShowVariables();
			if(showCtx)ShowContext();
		}
//******************* version ********************************
		else if(!strcmp(command,"ver")||!strcmp(command,"version")){
			if(running) Halt();
			j=1;
			bufferU[j++]=TX16;
			bufferU[j++]=0x1;
			bufferU[j++]=VER;		//version
			bufferU[j++]=0;
			bufferU[j++]=RX16;
			bufferU[j++]=0x1;
			bufferU[j++]=FLUSH;
			for(;j<DIMBUF;j++) bufferU[j]=0x0;
			write();
			msDelay(1+2*Tcom);
			read();
			if(saveLog)WriteLogIO();
			for(z=0;z<DIMBUF-2&&bufferI[z]!=RX16;z++);
			printf("debugger version: %d\n",bufferI[z+3]);	
		}
	}
	j=1;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;		//set D as input
	bufferU[j++]=EN_VPP_VCC;
	bufferU[j++]=0;
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(1);
	read();	
	if(saveLog)WriteLogIO();

#if !defined _WIN32 && !defined __CYGWIN__
	close(fd);
#endif
	return 0 ;
}

void Halt(){
	int j=1;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x6;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(2);
	read();
	if(saveLog)WriteLogIO();
	running=0;
	printf("halted\n");
}
int ReadRegister(int addr){
	int j=1,z;
	bufferU[j++]=TX16;
	bufferU[j++]=0x2;
	bufferU[j++]=RREG;		//Read register
	bufferU[j++]=0x1;		//1 byte
	bufferU[j++]=(addr>>8)&0xFF;
	bufferU[j++]=addr&0xFF;
	bufferU[j++]=RX16;
	bufferU[j++]=0x1;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;		//set D as input
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(1+3*Tcom);
	read();
	if(saveLog)WriteLogIO();
	for(z=0;z<DIMBUF-2&&bufferI[z]!=RX16;z++);
	return bufferI[z+3];	
}

int ReadRegisterN(int addr,int n,int* buf){
	int i,j=1,z,w;
	for(i=0;i<n;i+=w){
		w=i+(DIMBUF-9)/2<n?(DIMBUF-9)/2:n-i;
		bufferU[j++]=TX16;
		bufferU[j++]=0x2;
		bufferU[j++]=RREG;				//Read register
		bufferU[j++]=w;
		bufferU[j++]=(addr+i)>>8;
		bufferU[j++]=(addr+i)&0xFF;
		bufferU[j++]=RX16;
		bufferU[j++]=w;
		bufferU[j++]=FLUSH;
		for(;j<DIMBUF;j++) bufferU[j]=0x0;
		write();
		msDelay(1+(w+2)*Tcom);
		read();
		if(saveLog)WriteLogIO();
		for(z=0;z<DIMBUF-2&&bufferI[z]!=RX16;z++);
		for(j=0;j<w;j++) buf[i+j]=bufferI[z+3+j*2];
		j=1;
	}
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;		//set D as input
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(1);
	read();
	return i==n?0:-1;
}

void WriteRegister(int addr,int data){
	int j=1,z;
	bufferU[j++]=TX16;
	bufferU[j++]=0x2;
	bufferU[j++]=WREG;		//write register
	bufferU[j++]=data&0xFF;
	bufferU[j++]=(addr>>8)&0xFF;
	bufferU[j++]=addr&0xFF;
	bufferU[j++]=SET_CK_D;
	bufferU[j++]=0x2;		//set D as input
	bufferU[j++]=FLUSH;
	for(;j<DIMBUF;j++) bufferU[j]=0x0;
	write();
	msDelay(1+2*Tcom);
	read();	
	if(saveLog)WriteLogIO();
}

int ReadProgMem(int addr){
	int addr_temp, data_temp, eecon_temp,data;
	addr_temp=(ReadRegister(EEADRH)<<8)+ReadRegister(EEADR);
	data_temp=(ReadRegister(EEDATH)<<8)+ReadRegister(EEDATA);
	eecon_temp=ReadRegister(EECON1);
	WriteRegister(EEADRH,addr>>8);
	WriteRegister(EEADR,addr&0xFF);
	WriteRegister(EECON1,eecon_temp|0x80);	//EEPGD=1
	WriteRegister(EECON1,eecon_temp|0x81);	//EEPGD=1 + RD=1
	data=(ReadRegister(EEDATH)<<8)+ReadRegister(EEDATA);
	WriteRegister(EEADRH,addr_temp<<8);
	WriteRegister(EEADR,addr_temp&0xFF);
	WriteRegister(EEDATH,data_temp<<8);
	WriteRegister(EEDATA,data_temp&0xFF);
	WriteRegister(EECON1,eecon_temp);
	return data&0x3FFF;
}

int ReadDataMem(int addr){
	int addr_temp, data_temp, eecon_temp,data;
	addr_temp=ReadRegister(EEADR);
	data_temp=ReadRegister(EEDATA);
	eecon_temp=ReadRegister(EECON1);
	WriteRegister(EEADR,addr);
	WriteRegister(EECON1,eecon_temp&0x7F);			//EEPGD=0
	WriteRegister(EECON1,(eecon_temp&0x7F)|0x1);	//EEPGD=0 + RD=1
	data=ReadRegister(EEDATA);
	WriteRegister(EEADR,addr_temp);
	WriteRegister(EEDATA,data_temp);
	WriteRegister(EECON1,eecon_temp);
	return data;	
}

void ShowVariables(){
	int i,addr,data,s;
	char cmd[32];
	cmd[0]=0;
	if(!showVar) return;
	for(i=0;i<0x200;i++){
		if(variables[i].display&&variables[i].name) printf("0x%03X: %s = 0x%02X\n",i,variables[i].name,ReadRegister(i));
	}
}

void ShowContext(){
	int i,addr,data,s;
	char cmd[32];
	cmd[0]=0;
	addr=((ReadRegister(0x18E)&0x1F)<<8)+ReadRegister(0x18F);
	data=ReadProgMem(addr);		
	s=ReadRegister(status_temp);
	s=(s>>4)+((s<<4)&0xF0);		//STATUS is swapped
	printf("Next instruction: %s (0x%04X)\tPC=0x%04X\n",decodeCmd(data,cmd,(s&0x60)<<2),data,addr);
	printf("STATUS=0x%02X (",s);
	printf("%s ",s&0x80?"IRP":"   ");
	printf("%s ",s&0x40?"RP1":"   ");
	printf("%s ",s&0x20?"RP0":"   ");
	printf("%s ",s&0x10?"TO":"  ");
	printf("%s ",s&0x8?"PD":"  ");
	printf("%s ",s&0x4?"Z":" ");
	printf("%s ",s&0x2?"DC":"  ");
	printf("%s)\n",s&0x1?"C":" ");
	printf("W=0x%02X PCLATH=0x%02X FSR=0x%02X\n",ReadRegister(w_temp),ReadRegister(pclath_temp),ReadRegister(fsr_temp));		
}

#if !defined _WIN32 && !defined __CYGWIN__
DWORD GetTickCount(){
	struct timeb now;
	ftime(&now);
	return now.time*1000+now.millitm;
}
#endif

char* decodeCmd(int cmd,char *str, int addrH){
	char ins[32],reg[32];
	if((cmd&0x3F9F)==0) sprintf(str,"nop \t");
	else if(cmd==0x0008) sprintf(str,"return");
	else if(cmd==0x0009) sprintf(str,"retfie");
	else if(cmd==0x0063) sprintf(str,"sleep");
	else if(cmd==0x0064) sprintf(str,"clrwdt");
	else if((cmd>>12)==0){	//byte oriented instructions
		if((cmd>>8)==0&&cmd&0x80) sprintf(str,"movwf\t%s",getVar(addrH+(cmd&0x7F),reg));
		else if((cmd>>8)==1){
			if(cmd&0x80) sprintf(str,"clrf \t%s",getVar(addrH+(cmd&0x7F),reg));
			else sprintf(str,"clrf \tw");
		}
		else{
			switch(cmd>>8){
				case 2:
					sprintf(ins,"subwf");
				break;
				case 3:
					sprintf(ins,"decf");
				break;
				case 4:
					sprintf(ins,"iorwf");
				break;
				case 5:
					sprintf(ins,"andwf");
				break;
				case 6:
					sprintf(ins,"xorwf");
				break;
				case 7:
					sprintf(ins,"addwf");
				break;
				case 8:
					sprintf(ins,"movf");
				break;
				case 9:
					sprintf(ins,"comf");
				break;
				case 10:
					sprintf(ins,"incf");
				break;
				case 11:
					sprintf(ins,"decfsz");
				break;
				case 12:
					sprintf(ins,"rrf ");
				break;
				case 13:
					sprintf(ins,"rlf ");
				break;
				case 14:
					sprintf(ins,"swapf");
				break;
				case 15:
					sprintf(ins,"incfsz");
				break;
			}
			sprintf(str,"%s\t%s,%c",ins,getVar(addrH+(cmd&0x7F),reg),cmd&0x80?'f':'w');
		}
	}
	else if((cmd>>12)==1){	//bit oriented instructions
		switch(cmd>>10){
			case 4:
				sprintf(ins,"bcf ");
			break;
			case 5:
				sprintf(ins,"bsf ");
			break;
			case 6:
				sprintf(ins,"btfsc");
			break;
			case 7:
				sprintf(ins,"btfss");
			break;
			default:
				sprintf(ins,"??");	//(not possible)
		}
		sprintf(str,"%s\t%s,%d",ins,getVar(addrH+(cmd&0x7F),reg),(cmd&0x380)>>7);
	}
	else if((cmd>>12)==2) sprintf(str,"%s\t%X",cmd&0x800?"goto":"call",cmd&0x7FF);
	else if((cmd>>10)==0xC) sprintf(str,"movlw\t%X",cmd&0xFF);
	else if((cmd>>10)==0xD) sprintf(str,"retlw\t%X",cmd&0xFF);
	else if((cmd>>9)==0x1E) sprintf(str,"sublw\t%X",cmd&0xFF);
	else if((cmd>>9)==0x1F) sprintf(str,"addlw\t%X",cmd&0xFF);
	else if((cmd>>8)==0x38) sprintf(str,"iorlw\t%X",cmd&0xFF);
	else if((cmd>>8)==0x39) sprintf(str,"andlw\t%X",cmd&0xFF);
	else if((cmd>>8)==0x3A) sprintf(str,"xorlw\t%X",cmd&0xFF);
	else sprintf(str,"unknown command");
	return str;
}

// get register name from variable list or special registers
char* getVar(int addr,char *var){
	if(addr>0x200){		//memory bank is unknown; use only constant variables
		addr&=0x7F;
		if(addr==0) sprintf(var,"INDF");
		else if(addr==2) sprintf(var,"PCL");
		else if(addr==3) sprintf(var,"STATUS");
		else if(addr==4) sprintf(var,"FSR");
		else if(addr==0x0A) sprintf(var,"PCLATH");
		else if(addr==0x0B) sprintf(var,"INTCON");
		else if(addr>=0x70&&variables[addr].name) sprintf(var,variables[addr].name);
		else sprintf(var,"0x%03X",addr);
	}
	else{			//check all variables
		addr&=0x1FF;
		if(variables[addr].name) sprintf(var,variables[addr].name);
		else sprintf(var,"0x%03X",addr);
	}
	return var;
}
	

void WriteLogIO(){
	int i;
	fprintf(LogFile,"bufferU=[");
	for(i=0;i<DIMBUF;i++){
		if(i%32==0) fprintf(LogFile,"\n");
		fprintf(LogFile,"%02X ",bufferU[i]);
	}
	fprintf(LogFile,"]\n");
	fprintf(LogFile,"bufferI=[");
	for(i=0;i<DIMBUF;i++){
		if(i%32==0) fprintf(LogFile,"\n");
		fprintf(LogFile,"%02X ",bufferI[i]);
	}
	fprintf(LogFile,"]\n\n");
}


void msDelay(double delay)
{
#if !defined _WIN32 && !defined __CYGWIN__
	long x=(int)delay*1000.0;
	usleep(x>MinDly?x:MinDly);
#else
	Sleep((long)ceil(delay)>MinDly?(long)ceil(delay):MinDly);
#endif
}

int FindDevice(){
#if !defined _WIN32 && !defined __CYGWIN__
	if ((fd = open(path, O_RDONLY )) < 0) {
		perror("hiddev open");
		exit(1);
	}
	struct hiddev_devinfo device_info;
	ioctl(fd, HIDIOCGDEVINFO, &device_info);
	if(device_info.vendor!=vid||device_info.product!=pid){
		printf("can't find programmer\n");
		return -1;
	}
	else printf("programmer found\n");

	rep_info_u.report_type=HID_REPORT_TYPE_OUTPUT;
	rep_info_i.report_type=HID_REPORT_TYPE_INPUT;
	rep_info_u.report_id=rep_info_i.report_id=HID_REPORT_ID_FIRST;
	rep_info_u.num_fields=rep_info_i.num_fields=1;
	ref_multi_u.uref.report_type=HID_REPORT_TYPE_OUTPUT;
	ref_multi_i.uref.report_type=HID_REPORT_TYPE_INPUT;
	ref_multi_u.uref.report_id=ref_multi_i.uref.report_id=HID_REPORT_ID_FIRST;
	ref_multi_u.uref.field_index=ref_multi_i.uref.field_index=0;
	ref_multi_u.uref.usage_index=ref_multi_i.uref.usage_index=0;
	ref_multi_u.num_values=ref_multi_i.num_values=DIMBUF;

#else
	char string[256];
	PSP_DEVICE_INTERFACE_DETAIL_DATA detailData;
	HANDLE DeviceHandle;
	HANDLE hDevInfo;
	GUID HidGuid;
	int MyDeviceDetected; 
	char MyDevicePathName[1024];
	ULONG Length;
	ULONG Required;
	typedef struct _HIDD_ATTRIBUTES {
	    ULONG   Size;
	    USHORT  VendorID;
	    USHORT  ProductID;
	    USHORT  VersionNumber;
	} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

	typedef void (__stdcall*GETHIDGUID) (OUT LPGUID HidGuid);
	typedef BOOLEAN (__stdcall*GETATTRIBUTES)(IN HANDLE HidDeviceObject,OUT PHIDD_ATTRIBUTES Attributes);
	typedef BOOLEAN (__stdcall*SETNUMINPUTBUFFERS)(IN  HANDLE HidDeviceObject,OUT ULONG  NumberBuffers);
	typedef BOOLEAN (__stdcall*GETNUMINPUTBUFFERS)(IN  HANDLE HidDeviceObject,OUT PULONG  NumberBuffers);
	typedef BOOLEAN (__stdcall*GETFEATURE) (IN  HANDLE HidDeviceObject, OUT PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*SETFEATURE) (IN  HANDLE HidDeviceObject, IN PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*GETREPORT) (IN  HANDLE HidDeviceObject, OUT PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*SETREPORT) (IN  HANDLE HidDeviceObject, IN PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*GETMANUFACTURERSTRING) (IN  HANDLE HidDeviceObject, OUT PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*GETPRODUCTSTRING) (IN  HANDLE HidDeviceObject, OUT PVOID ReportBuffer, IN ULONG ReportBufferLength);
	typedef BOOLEAN (__stdcall*GETINDEXEDSTRING) (IN  HANDLE HidDeviceObject, IN ULONG  StringIndex, OUT PVOID ReportBuffer, IN ULONG ReportBufferLength);
	HIDD_ATTRIBUTES Attributes;
	SP_DEVICE_INTERFACE_DATA devInfoData;
	int LastDevice = FALSE;
	int MemberIndex = 0;
	LONG Result;
	char UsageDescription[256];

	Length=0;
	detailData=NULL;
	DeviceHandle=NULL;

	HMODULE hHID=0;
	GETHIDGUID HidD_GetHidGuid=0;
	GETATTRIBUTES HidD_GetAttributes=0;
	SETNUMINPUTBUFFERS HidD_SetNumInputBuffers=0;
	GETNUMINPUTBUFFERS HidD_GetNumInputBuffers=0;
	GETFEATURE HidD_GetFeature=0;
	SETFEATURE HidD_SetFeature=0;
	GETREPORT HidD_GetInputReport=0;
	SETREPORT HidD_SetOutputReport=0;
	GETMANUFACTURERSTRING HidD_GetManufacturerString=0;
	GETPRODUCTSTRING HidD_GetProductString=0;
	hHID = LoadLibrary("hid.dll");
	if(!hHID){ 
		printf("Can't find hid.dll");
		return 0;
	}
	HidD_GetHidGuid=(GETHIDGUID)GetProcAddress(hHID,"HidD_GetHidGuid");
	HidD_GetAttributes=(GETATTRIBUTES)GetProcAddress(hHID,"HidD_GetAttributes");
	HidD_SetNumInputBuffers=(SETNUMINPUTBUFFERS)GetProcAddress(hHID,"HidD_SetNumInputBuffers");
	HidD_GetNumInputBuffers=(GETNUMINPUTBUFFERS)GetProcAddress(hHID,"HidD_GetNumInputBuffers");
	HidD_GetFeature=(GETFEATURE)GetProcAddress(hHID,"HidD_GetFeature");
	HidD_SetFeature=(SETFEATURE)GetProcAddress(hHID,"HidD_SetFeature");
	HidD_GetInputReport=(GETREPORT)GetProcAddress(hHID,"HidD_GetInputReport");
	HidD_SetOutputReport=(SETREPORT)GetProcAddress(hHID,"HidD_SetOutputReport");
	HidD_GetManufacturerString=(GETMANUFACTURERSTRING)GetProcAddress(hHID,"HidD_GetManufacturerString");
	HidD_GetProductString=(GETPRODUCTSTRING)GetProcAddress(hHID,"HidD_GetProductString");
	if(HidD_GetHidGuid==NULL\
		||HidD_GetAttributes==NULL\
		||HidD_GetFeature==NULL\
		||HidD_SetFeature==NULL\
		||HidD_GetInputReport==NULL\
		||HidD_SetOutputReport==NULL\
		||HidD_GetManufacturerString==NULL\
		||HidD_GetProductString==NULL\
		||HidD_SetNumInputBuffers==NULL\
		||HidD_GetNumInputBuffers==NULL) return -1;


	HMODULE hSAPI=0;
	hSAPI = LoadLibrary("setupapi.dll");
	if(!hSAPI){ 
		printf("Can't find setupapi.dll");
		return 0;
	}
	typedef HDEVINFO (WINAPI* SETUPDIGETCLASSDEVS) (CONST GUID*,PCSTR,HWND,DWORD);
	typedef BOOL (WINAPI* SETUPDIENUMDEVICEINTERFACES) (HDEVINFO,PSP_DEVINFO_DATA,CONST GUID*,DWORD,PSP_DEVICE_INTERFACE_DATA);
	typedef BOOL (WINAPI* SETUPDIGETDEVICEINTERFACEDETAIL) (HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_A,DWORD,PDWORD,PSP_DEVINFO_DATA);
	typedef BOOL (WINAPI* SETUPDIDESTROYDEVICEINFOLIST) (HDEVINFO);	
	SETUPDIGETCLASSDEVS SetupDiGetClassDevsA=0;
	SETUPDIENUMDEVICEINTERFACES SetupDiEnumDeviceInterfaces=0;
	SETUPDIGETDEVICEINTERFACEDETAIL SetupDiGetDeviceInterfaceDetailA=0;
	SETUPDIDESTROYDEVICEINFOLIST SetupDiDestroyDeviceInfoList=0;
	SetupDiGetClassDevsA=(SETUPDIGETCLASSDEVS) GetProcAddress(hSAPI,"SetupDiGetClassDevsA");
	SetupDiEnumDeviceInterfaces=(SETUPDIENUMDEVICEINTERFACES) GetProcAddress(hSAPI,"SetupDiEnumDeviceInterfaces");
	SetupDiGetDeviceInterfaceDetailA=(SETUPDIGETDEVICEINTERFACEDETAIL) GetProcAddress(hSAPI,"SetupDiGetDeviceInterfaceDetailA");
	SetupDiDestroyDeviceInfoList=(SETUPDIDESTROYDEVICEINFOLIST) GetProcAddress(hSAPI,"SetupDiDestroyDeviceInfoList");
	if(SetupDiGetClassDevsA==NULL\
		||SetupDiEnumDeviceInterfaces==NULL\
		||SetupDiDestroyDeviceInfoList==NULL\
		||SetupDiGetDeviceInterfaceDetailA==NULL) return -1;
	
	
	/*
	The following code is adapted from Usbhidio_vc6 application example by Jan Axelson
	for more information see see http://www.lvr.com/hidpage.htm
	*/

	/*
	API function: HidD_GetHidGuid
	Get the GUID for all system HIDs.
	Returns: the GUID in HidGuid.
	*/
	HidD_GetHidGuid(&HidGuid);

	/*
	API function: SetupDiGetClassDevs
	Returns: a handle to a device information set for all installed devices.
	Requires: the GUID returned by GetHidGuid.
	*/
	hDevInfo=SetupDiGetClassDevs(&HidGuid,NULL,NULL,DIGCF_PRESENT|DIGCF_INTERFACEDEVICE);
	devInfoData.cbSize = sizeof(devInfoData);

	//Step through the available devices looking for the one we want.
	//Quit on detecting the desired device or checking all available devices without success.
	MemberIndex = 0;
	LastDevice = FALSE;
	do
	{
		/*
		API function: SetupDiEnumDeviceInterfaces
		On return, MyDeviceInterfaceData contains the handle to a
		SP_DEVICE_INTERFACE_DATA structure for a detected device.
		Requires:
		The DeviceInfoSet returned in SetupDiGetClassDevs.
		The HidGuid returned in GetHidGuid.
		An index to specify a device.
		*/
		Result=SetupDiEnumDeviceInterfaces (hDevInfo, 0, &HidGuid, MemberIndex, &devInfoData);
		if (Result != 0)
		{
			//A device has been detected, so get more information about it.
			/*
			API function: SetupDiGetDeviceInterfaceDetail
			Returns: an SP_DEVICE_INTERFACE_DETAIL_DATA structure
			containing information about a device.
			To retrieve the information, call this function twice.
			The first time returns the size of the structure in Length.
			The second time returns a pointer to the data in DeviceInfoSet.
			Requires:
			A DeviceInfoSet returned by SetupDiGetClassDevs
			The SP_DEVICE_INTERFACE_DATA structure returned by SetupDiEnumDeviceInterfaces.

			The final parameter is an optional pointer to an SP_DEV_INFO_DATA structure.
			This application doesn't retrieve or use the structure.
			If retrieving the structure, set
			MyDeviceInfoData.cbSize = length of MyDeviceInfoData.
			and pass the structure's address.
			*/
			//Get the Length value.
			//The call will return with a "buffer too small" error which can be ignored.
			Result = SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInfoData, NULL, 0, &Length, NULL);

			//Allocate memory for the hDevInfo structure, using the returned Length.
			detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(Length);

			//Set cbSize in the detailData structure.
			detailData -> cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			//Call the function again, this time passing it the returned buffer size.
			Result = SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInfoData, detailData, Length,&Required, NULL);

			// Open a handle to the device.
			// To enable retrieving information about a system mouse or keyboard,
			// don't request Read or Write access for this handle.
			/*
			API function: CreateFile
			Returns: a handle that enables reading and writing to the device.
			Requires:
			The DevicePath in the detailData structure
			returned by SetupDiGetDeviceInterfaceDetail.
			*/
			DeviceHandle=CreateFile(detailData->DevicePath,
				0, FILE_SHARE_READ|FILE_SHARE_WRITE,
				(LPSECURITY_ATTRIBUTES)NULL,OPEN_EXISTING, 0, NULL);

			/*
			API function: HidD_GetAttributes
			Requests information from the device.
			Requires: the handle returned by CreateFile.
			Returns: a HIDD_ATTRIBUTES structure containing
			the Vendor ID, Product ID, and Product Version Number.
			Use this information to decide if the detected device is
			the one we're looking for.
			*/

			//Set the Size to the number of bytes in the structure.
			Attributes.Size = sizeof(Attributes);
			Result = HidD_GetAttributes(DeviceHandle,&Attributes);

			//Is it the desired device?
			MyDeviceDetected = FALSE;
			char a[256];
			if (Attributes.VendorID == vid)
			{
				if (Attributes.ProductID == pid)
				{
					//Both the Vendor ID and Product ID match.
					MyDeviceDetected = TRUE;
					strcpy(MyDevicePathName,detailData->DevicePath);

					// Get a handle for writing Output reports.
					WriteHandle=CreateFile(detailData->DevicePath,
						GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
						(LPSECURITY_ATTRIBUTES)NULL,OPEN_EXISTING,0,NULL);

					//Get a handle to the device for the overlapped ReadFiles.
					ReadHandle=CreateFile(detailData->DevicePath,
						GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,(LPSECURITY_ATTRIBUTES)NULL,
						OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);

					if (hEventObject) CloseHandle(hEventObject);
					hEventObject = CreateEvent(NULL,TRUE,TRUE,"");

					//Set the members of the overlapped structure.
					HIDOverlapped.hEvent = hEventObject;
					HIDOverlapped.Offset = 0;
					HIDOverlapped.OffsetHigh = 0;
					Result=HidD_SetNumInputBuffers(DeviceHandle,64);
				}
				else
					//The Product ID doesn't match.
					CloseHandle(DeviceHandle);
			}
			else
				//The Vendor ID doesn't match.
				CloseHandle(DeviceHandle);
		//Free the memory used by the detailData structure (no longer needed).
		free(detailData);
		}
		else
			//SetupDiEnumDeviceInterfaces returned 0, so there are no more devices to check.
			LastDevice=TRUE;
		//If we haven't found the device yet, and haven't tried every available device,
		//try the next one.
		MemberIndex = MemberIndex + 1;
	} //do
	while ((LastDevice == FALSE) && (MyDeviceDetected == FALSE));
	//Free the memory reserved for hDevInfo by SetupDiClassDevs.
	SetupDiDestroyDeviceInfoList(hDevInfo);

	if (MyDeviceDetected == FALSE){
		printf("Can't find device\n");
		return -1;
	}

	if(info){
		printf("Device detected: vid=0x%04X pid=0x%04X\nPath: %s\n",vid,pid,MyDevicePathName);
		if(HidD_GetManufacturerString(DeviceHandle,string,sizeof(string))==TRUE) wprintf(L"Manufacturer string: %s\n",string);
		if(HidD_GetProductString(DeviceHandle,string,sizeof(string))==TRUE) wprintf(L"Product string: %s\n",string);
	}
#endif
	return 0;
}
