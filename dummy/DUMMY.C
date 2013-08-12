/*******************************************************************************
	Dummy UPS driver
	Copyright ¸ 2005 Peter Koller, Maison Anglais. All Rights Reserved

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.
	
	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
	
	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************************************/
#define INCL_WIN
#define INCL_DOS
#define	INCL_ERRORS

#include	<os2.h>
#include	<ctype.h>
#include	<math.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<sys\time.h>
#include	"..\common.h"
#include	"msgx.h"

//the maximum length of messages expected from the UPS
#define				SZ_INBOUND_BUFFER	256

//the approximate minimum poll interval
#define				MIN_POLL_INTERVAL	350
#define				MAX_POLL_INTERVAL	800

//the variation from nominal of a charged and discharged battery
#define				DISCHARGE_RATIO		0.9
#define				CHARGE_RATIO		1.2

#define		OP_MODE		(OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE)

enum	{UPS_NONE, UPS_Disable, UPS_Query, UPS_Test, UPS_TestL, UPS_Quiet, UPS_Shut, UPS_Cancel, UPS_CancTst, UPS_Inf, UPS_Rate};

typedef	struct	_upsproduct
	{
		char	name[32];
		char	model[32];
		char	version[32];
		MIXED	voltage;
		MIXED	current;
		MIXED	battery;
		MIXED	frequency;
	}	UPSPRODUCT, *PUPSPRODUCT;

PSZ		msg[] =
	{
		"",
		"D\r",
		"Q1\r",
		"T%s\r",
		"TL\r",
		"Q\r",
		"S%.2s\r",
		"C\r",
		"CT\r",
		"I\r",
		"F\r"
	};

/* For DosDevIOCtl() */
#define		DIO_SERIAL	0x01

#define		SER_BAUD	0x41
#define		SER_MODE	0x42

/*** local parameters ***/
HFILE				hFile = {0};
int					opencount = 0;
int					threadid = 0;
int					threadfail;
int					lastmessage = UPS_NONE;
int					curmessage = UPS_NONE;
static	BOOL		WaitFlag = FALSE;
static	BOOL		StopPoll = FALSE;
UPSSTATUS			upst = {0};
UPSPRODUCT			upspr = {0};
static	char		buffer[SZ_INBOUND_BUFFER];
static	char		infostr[1024];
static	char		dummyinf[] = "#DUMMY PowerFake 1.0\r";
static	char		dummyrate[] = "#230.0 006 12.00 50.0\r";
static	char		dummy[128] = "(208.4 140.0 208.4 034 59.9 12.5 35.0 00001000\r";

/*** global functions ***/
int		APIENTRY	UpsPortlist(HWND hWndDlg, int id, char *parms);
int		APIENTRY	UpsStatus(HAB hab, ULONG msg, MPARAM mp1, PUPSSTATUS pups);

/*** local functions ***/
int 				OpenComPort(char *port);
int 				CloseComPort(void);
BOOL 				TimeoutDlgThread(PTID pTid, ULONG ulNtimeslices);
int					SendUPSMsg(int id, MPARAM val);
int					UpdateUPSStatus(char *string, PUPSSTATUS pups);
void	_Optlink	MessageThread(void* handle);
int					waitformessage(HAB hab, int idmsg, PUPSSTATUS pups);
int					bstrtoint(char* str, char **rets);

/*******************************************************************************
    skips text to find elements in the postscript file
*******************************************************************************/
inline	int iswhite(char ch)
	{
        if(isspace(ch)) return TRUE;
        else if(ch == '\r') return TRUE;
        else if(ch == '\n') return TRUE;
        return FALSE;
	}

inline	ULONG	Skipspace(PCHAR txt)
	{
		register ULONG   TP = 0;
		while(iswhite(*(txt + TP))) TP++;
		return TP;
	}

inline	ULONG	Skipelse(PCHAR txt)
	{
		register ULONG   TP = 0;
		while(!isspace(*(txt + TP)) && (*(txt + TP) != '\0')) TP++;
		return TP;
	}

/*******************************************************************************
    extern message is called by a calling application
*******************************************************************************/
int  APIENTRY UpsPortlist(HWND hWndDlg, int id, char *parms)
	{
		int  	c, iCommPorts;
		LONG	cur;
		char	string[20];
		BYTE  	cCommPorts;

		DosDevConfig (&cCommPorts, DEVINFO_RS232);
		iCommPorts = (int)cCommPorts;
		WinEnableWindowUpdate(WinWindowFromID(hWndDlg, id), FALSE);

		WinSendDlgItemMsg(hWndDlg, id, LM_DELETEALL, 0, 0);
		for(c = 0; c < iCommPorts; c++)
			{
				sprintf(string, "com%d", (c + 1));
				cur = (LONG)WinSendDlgItemMsg(hWndDlg, id, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP(string));
				if(!stricmp(parms, string)) WinSendDlgItemMsg(hWndDlg, id, LM_SELECTITEM, MPFROMSHORT(cur), MPFROMSHORT(TRUE));
			}
		WinEnableWindowUpdate(WinWindowFromID(hWndDlg, id), TRUE);
		return 0;
	}


/*******************************************************************************
    extern message is called by a calling application
*******************************************************************************/
int  APIENTRY UpsStatus(HAB hab, ULONG msg, MPARAM mp1, PUPSSTATUS pups)
	{
		int	retcode = ERROR_BAD_COMMAND;
		int	x;
		switch(msg)
			{
				case cmd_init:
					memset(&upst, 0, sizeof(UPSSTATUS));
					upst.size = sizeof(UPSSTATUS);
					upst.strucid = STRUCTURE_ID;
					if(0 != (retcode = CloseComPort())) break;		//stops any poll, and inits the various flags
					if(pups)
						{
							upst.msgfile = pups->msgfile;
							if(upst.msgfile) sprintmsg(infostr, upst.msgfile, "CONNECT1");
							pups->infostr = infostr;
						}
					if(0 != (retcode = OpenComPort((char*)mp1))) break;
					threadid = _beginthread(MessageThread, NULL, 0x4000, NULL);
					DosSleep(1);
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(0 != (retcode = SendUPSMsg(UPS_Inf, NULL))) break;
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(0 != (retcode = SendUPSMsg(UPS_Rate, NULL))) break;
					retcode = waitformessage(hab, 0, pups);
					break;
				case cmd_info:
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(upst.msgfile) sprintmsg(infostr, upst.msgfile, "MEGATEC1",
							upspr.name, upspr.model, upspr.version,
							Mix_to_d(upspr.voltage), Mix_to_d(upspr.current), Mix_to_d(upspr.frequency),
							Mix_to_d(upspr.battery));
					break;
				case cmd_quiet:
					StopPoll = TRUE;
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(_flagtest(upst.upsstatus, stat_buzzer) ^ (!mp1))
						{
							if(0 != (retcode = SendUPSMsg(UPS_Quiet, NULL))) break;
							retcode = waitformessage(hab, 0, pups);
							break;
						}
					else retcode = NO_ERROR;
					break;
				case cmd_test:
					StopPoll = TRUE;
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(((int)mp1) == -1) retcode = SendUPSMsg(UPS_TestL, NULL);
					retcode = SendUPSMsg(UPS_Test, mp1);
					if(0 != retcode) break;
					StopPoll = FALSE;
					retcode = waitformessage(hab, 0, pups);
					break;
				case cmd_abort:
					StopPoll = TRUE;
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(_flagtest(upst.upsstatus, stat_testing)) retcode = SendUPSMsg(UPS_CancTst, NULL);
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(_flagtest(upst.upsstatus, stat_shutting)) retcode = SendUPSMsg(UPS_Cancel, NULL);
					if(0 != retcode) break;
					StopPoll = FALSE;
					retcode = waitformessage(hab, 0, pups);
					break;
				case cmd_shutdown:
					StopPoll = TRUE;
					if(0 != (retcode = waitformessage(hab, 0, NULL))) break;
					if(0 != (retcode = SendUPSMsg(UPS_Shut, mp1))) break;
					StopPoll = FALSE;
					retcode = waitformessage(hab, 0, pups);
					break;
				case cmd_exit:
					retcode = CloseComPort();
					return retcode;
				case cmd_poll:
					retcode = waitformessage(hab, 0, pups);
					pups->infostr = infostr;
					break;
				default:
					retcode = waitformessage(hab, 0, NULL);
					break;
			}
		StopPoll = FALSE;
		return retcode;
	}

/*******************************************************************************
	Wait for a particular message
*******************************************************************************/
int	waitformessage(HAB hab, int idmsg, PUPSSTATUS pups)
	{
		QMSG			qmsg;
		int				retcode;
		ULONG			poll, time = clock();

		if(!threadid)  return threadfail;
		for(;;)
			{
				if(!threadid) break;
				if(!lastmessage)
					{
						if(!WaitFlag && !idmsg) break;
						else if(!WaitFlag && (curmessage == idmsg)) break;
					}
				DosSleep(1);
				poll = ((clock() - time) * 1000) / CLOCKS_PER_SEC;
				if(poll > MAX_POLL_INTERVAL) return ERROR_UPS_TIMEOUT;
			}
		if(!threadid)  return threadfail;
		if(WaitFlag) return ERROR_UPS_BUSY;
		if(pups)
			{
				memmove(&(pups->upsstatus), &(upst.upsstatus), sizeUPSstat(pups));
				pups->infostr = infostr;
			}
		return NO_ERROR;
	}

/*******************************************************************************
	Open the com port
*******************************************************************************/
int OpenComPort(char *port)
	{
		return NO_ERROR;
	}

/*******************************************************************************
	Close the com port
*******************************************************************************/
int CloseComPort(void)
	{
		if(threadid)
			{
                ULONG tid = threadid;
				threadid = 0;
				TimeoutDlgThread(&tid, 2000);
			}
		lastmessage = curmessage = 0;
		threadfail = 0;
		WaitFlag = FALSE;
		StopPoll = TRUE;
		return NO_ERROR;
	}

/*******************************************************************************
    Timeout waiting for a thread to terminate. Alarms if unsucessful.
*******************************************************************************/
BOOL TimeoutDlgThread(PTID pTid, ULONG ulNtimeslices)
    {
        APIRET  rc;
        do  {
                rc = DosWaitThread(pTid, DCWW_NOWAIT);
                if(rc == ERROR_THREAD_NOT_TERMINATED)
                    {
                        DosSleep(1);
                        ulNtimeslices--;
                    }
                else if(rc == NO_ERROR) return(TRUE);
                else return(FALSE);
            }   while(ulNtimeslices);
        if(!ulNtimeslices) WinAlarm(HWND_DESKTOP, WA_ERROR);
        return(FALSE);
    }

/*******************************************************************************
	Send a message to the com port
*******************************************************************************/
int SendUPSMsg(int id, MPARAM val)
	{
		lastmessage = 0;
		switch(id)
			{
				case UPS_Query:
				case UPS_Inf:
				case UPS_Rate:
					lastmessage = id;
				default:
					break;
			}
		return NO_ERROR;
	}

/*******************************************************************************
    squirts any text coming from the UPS into a membuffer
*******************************************************************************/
void    _Optlink    MessageThread(void* handle)
	{
		ULONG			time = clock();
		ULONG			poll;
		ULONG           cbRead;
		ULONG           count = 0;
		ULONG			upsmsg = 0;
		char			*stringend = NULL;


		memset(buffer, 0, SZ_INBOUND_BUFFER);
		threadfail = NO_ERROR;
		do	{
				DosSleep(100);
				WaitFlag = TRUE;
				DosSleep(1);
				threadfail = UpdateUPSStatus(NULL, &upst);
				if(!threadid  || threadfail) break;
				WaitFlag = FALSE;
				do	{
						poll = ((clock() - time) * 1000) / CLOCKS_PER_SEC;
						DosSleep(100);
						if(!threadid  || threadfail) break;
					}	while(poll < MIN_POLL_INTERVAL);
				upst.pollinterval = poll;
				time = clock();
				
				DosSleep(1);
				if(!lastmessage && !StopPoll) 
					{
						if(upsmsg == 60) threadfail = SendUPSMsg(UPS_Inf, NULL);
						if(upsmsg == 120) threadfail = SendUPSMsg(UPS_Rate, NULL);
						else threadfail = SendUPSMsg(UPS_Query, NULL);
						(upsmsg < 120)?(upsmsg++):(upsmsg = 0);
					}
				if(!threadid  || threadfail) break;
				DosSleep(1);
			}	while(threadid);
		threadid = 0;
		WaitFlag = FALSE;
	}

/*******************************************************************************
	Process the string from the UPS

		int		upsstatus;
		int		loadstate;
		int		ipfreq;
		int		opfreq;
		int		tempc;
		int		batpc;
		MIXED	voltin;
		MIXED	voltout;
		MIXED	voltbatt;

*******************************************************************************/
int	UpdateUPSStatus(char *string, PUPSSTATUS pups)
	{
		char	*stp;
		char	*stp2;
		int		len;
		double	val = 0;
		double	vin = 0;
		double	vbrk = 0;
		double	vout = 0;

		switch(lastmessage)
			{
				case UPS_Disable:
				case UPS_Query:
					if(pups->msgfile) sprintmsg(dummy, upst.msgfile, "DUMMY001");
					string = dummy;
					if(*string != '(') break;
					pups->upsstatus = 0;

					vin = strtod(string + 1, &stp);
					pups->voltin = d_to_Mix(vin);
					stp += Skipspace(stp);
					pups->upsstatus |= valid_voltin;

					vbrk = strtod(stp, &stp);
					pups->voltbrk = d_to_Mix(vbrk);
					stp += Skipspace(stp);
					pups->upsstatus |= valid_voltbrk;

					vout = strtod(stp, &stp);
					pups->voltout = d_to_Mix(vout);
					stp += Skipspace(stp);
					pups->upsstatus |= valid_voltout;

					pups->loadstate = strtol(stp, &stp, 10);
					stp += Skipspace(stp);
					pups->upsstatus |= valid_loadstate;

					pups->ipfreq = pups->opfreq = (int)strtod(stp, &stp);
					stp += Skipspace(stp);
					if(pups->ipfreq > 0) pups->upsstatus |= (valid_opfreq | valid_ipfreq);

					val = strtod(stp, &stp);
					pups->voltbatt = d_to_Mix(val);
					stp += Skipspace(stp);
					if(val > 0) pups->upsstatus |= valid_voltbatt;
					if((Mix_to_d(upspr.battery) > 0) && (val > 0))
						{
							double mini, defl, maxi;
							defl = Mix_to_d(upspr.battery);
							mini = (defl * DISCHARGE_RATIO); maxi = (defl * CHARGE_RATIO);
							defl = maxi - mini; val -= mini; if(val < 0) val = 0;
							pups->batpc = (int)((double)(val / defl) * 100);
//							pups->batpc = (int)((double)(Mix_to_d(upspr.battery) / (val * 1.2)) * 100);
							pups->upsstatus |= valid_batpc;
						}

					val = strtod(stp, &stp);
					pups->tempc = d_to_Mix(val);
					stp += Skipspace(stp);
					pups->upsstatus |= valid_tempc;
					pups->upsstatus |= bstrtoint(stp, &stp);
					if(_flagtest(pups->upsstatus, stat_failed)) pups->upsstatus |= stat_buzzena;
					if(_flagtest(pups->upsstatus, stat_testing)) pups->upsstatus |= stat_buzzena;
					if(_flagtest(pups->upsstatus, stat_shutting)) pups->upsstatus |= stat_buzzena;
					
					if(_flagtest(pups->upsstatus, stat_boost))
						{
							if(vbrk < vout) pups->upsstatus |= (stat_glitch | stat_boost);
							if(vin > vout)
								{
									pups->upsstatus &= ~stat_boost;
									pups->upsstatus |= stat_buck;
								}
						}
					break;
				case UPS_Inf:
					string = dummyinf;
					if(*string != '#') break;

					len = Skipelse(string + 1);
					stp = (string + 1 + len); len = min(len, 15);
					strncpy(upspr.name, string + 1, len); *((upspr.name) + len) = '\0';
					stp += Skipspace(stp); stp2 = stp;

					len = Skipelse(stp);
					stp = (stp + len); len = min(len, 15);
					strncpy(upspr.model, stp2, len); *((upspr.model) + len) = '\0';
					stp += Skipspace(stp); stp2 = stp;

					len = Skipelse(stp);
					stp = (stp + len); len = min(len, 15);
					strncpy(upspr.version, stp2, len); *((upspr.version) + len) = '\0';
					stp += Skipspace(stp); stp2 = stp;
					break;
				case UPS_Rate:
					string = dummyrate;
					if(*string != '#') break;

					val = strtod(string + 1, &stp);
					upspr.voltage = d_to_Mix(val);
					stp += Skipspace(stp);

					val = strtod(stp, &stp);
					upspr.current = d_to_Mix(val);
					stp += Skipspace(stp);

					val = strtod(stp, &stp);
					upspr.battery = d_to_Mix(val);
					stp += Skipspace(stp);

					val = strtod(stp, &stp);
					upspr.frequency = d_to_Mix(val);
					stp += Skipspace(stp);
					break;
				default:
					break;
			}
		curmessage = lastmessage;
		lastmessage = 0;
		return 0;
	}

int	bstrtoint(char* str, char **rets)
	{
		register int	x;
		int 			bit = 0x80;
		int 			ret = 0;
		for(x = 0; x < 8; x++)
			{
				if(*(str + x) == '\0') break;
				if(*(str + x) == '1') ret |= (bit >> x);
			}
		if(rets) *rets = (str + x);
		return ret;
	}
