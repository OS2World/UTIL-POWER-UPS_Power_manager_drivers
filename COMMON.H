/*******************************************************************************
	Common UPS Interface header
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
/*******************************************************************************
	Generic UPS interface definition is designed to manage UPS units in common
	manner. Different UPS units can be supported by the use of drivers which 
	all talk the same language. As the language is likely to evolve, some
	checking of versions is provided.
	
	The process involves just one message passing from the application to the
	driver.
	The driver is responsible for polling the UPS at intervals set by the poll 
	interval. The calling application can poll the driver at regular intervals 
	to get status information. This provides a double buffering effect. It 
	also means that the driver could be designed to talk to more than one 
	application. 
*******************************************************************************/
#ifndef COMMON_UPS_CONTROLS
#define COMMON_UPS_CONTROLS

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef _WIN32		//if compiling for windows
#ifndef __cplusplus
	#define	inline				_inline
#endif /* __cplusplus */
#define	APIFNPTR		APIENTRY*
#endif

#ifdef __OS2__		//if compiling for os2
#ifndef __cplusplus
	#define	inline				_Inline
#endif /* __cplusplus */
#define	APIFNPTR		*APIENTRY
#endif

#define				_flagtest(Flags, Key) (BOOL)((Key) == ((Flags) & (Key)))

//version = u100 
#define STRUCTURE_ID		'001u'

//commands to ups
typedef enum {cmd_poll, cmd_init, cmd_info, cmd_quiet, cmd_test, cmd_abort, cmd_shutdown, cmd_exit};

//mparam for cmd_init: an init string unique to driver such as (example: "com1:2400,n,8,1")
//mparam for cmd_info:  nothing
//mparam for cmd_quiet:	1 = yes, 0 = no
//mparam for cmd_test: the test time, where 0 is default, -1 until flat battery, other is time in minutes (MIXED).val .
//mparam for cmd_abort: the command to abort
//mparam for cmd_shutdown: the time to shutdown where 0 is now, other is time in minutes (MIXED).val .
//mparam for cmd_exit: none, close the communication to the UPS.

//returns from ups are DOS api returns
#define	ERROR_UPS_BUSY			-1		/* This is a soft failure */
#define	ERROR_UPS_TIMEOUT		-2		/* This is a hard failure, the UPS is not responding */
#define	ERROR_THREAD_FAIL		-3		/* This is a hard failure, the read thread cannot be started */

//returns from upsstatus
#define 	stat_buzzer 		0x00000001		/* status of buzzer - when it is enabled */
#define 	stat_buzzena 		0x00000100		/* buzzer enabled */

#define 	stat_shutting 		0x00000002		/* The UPS is shutting down in the near future */
#define 	stat_testing 		0x00000004		/* The UPS is running a battery test */

#define 	stat_upstype 		0x00000008		/* If true, then this is a standby type unit */
#define 	stat_upsfail 		0x00000010		/* The UPS has failed */

#define 	stat_boost 			0x00000020		/* The UPS is boosting the i/p voltage */
#define 	stat_buck 			0x00000200		/* The UPS is bucking the i/p voltage */
#define 	stat_glitch			0x00000400		/* A mains glitch has been registered */

#define 	stat_battery 		0x00000040		/* The UPS battery is low/critical */
#define 	stat_failed 		0x00000080		/* The mains input power has failed */

#define		valid_loadstate 	0x00010000		/* Is valid field - ups load state in percent */
#define		valid_ipfreq 		0x00020000		/* Is valid field - mains input frequency */
#define		valid_opfreq 		0x00040000		/* Is valid field - mains output frequency */
#define		valid_batpc 		0x00080000		/* Is valid field - ups battery state in percent */
#define		valid_tempc 		0x00100000		/* Is valid field - ups temperature in degrees */
#define		valid_voltin 		0x00200000		/* Is valid field - mains input voltage */
#define		valid_voltbrk 		0x00400000		/* Is valid field - worst state input voltage */
#define		valid_voltout 		0x00800000		/* Is valid field - mains output voltage */
#define		valid_voltbatt 		0x01000000		/* Is valid field - battery voltage */

typedef	union	_mixed
	{
		struct
			{
				unsigned short remain;
				unsigned short integer;
			} 	set;
		unsigned int	val;
	}	MIXED;
	
inline MIXED d_to_Mix(double d)
	{
		MIXED m;
		d *= 0x10000;
		m.val = ((int)(d));
		return m;
	}

inline double Mix_to_d(MIXED m)
	{
		double d = ((double)(m.val));
		d /= 0x10000;
		return d;
	}

inline char* Mix_to_char(MIXED m, char *buff)
	{
		double d = ((double)(m.val));
		d /= 0x10000;
		sprintf(buff, "%g", d);
		return buff;
	}

typedef	struct	_upsstatus
	{
		int		size;				/* size of structure */
		int		strucid;			/* structure version control */
		FILE*	msgfile;			/* a message file handle for messages */
		int		upsstatus;			/* ups status flags */
		int		pollinterval;		/* ups poll interval, zero until polling */
		int		loadstate;			/* ups load state in percent */
		int		ipfreq;				/* mains input frequency */
		int		opfreq;				/* mains output frequency */
		int		batpc;				/* ups battery state in percent */
		MIXED	tempc;				/* ups temperature in degrees */
		MIXED	voltin;				/* mains input voltage */
		MIXED	voltbrk;			/* worst state input voltage */
		MIXED	voltout;			/* mains output voltage */
		MIXED	voltbatt;			/* battery voltage */
		PCHAR	infostr;			/* pointer to an info string from the ups */
	}	UPSSTATUS, *PUPSSTATUS;

#define	sizeUPSstat(pups) 		(((PUPSSTATUS)(pups))->size - sizeof(int[3]))

//the first two parameters of UPSSTATUS must be set before calling driver (sanity check!!)

typedef int  (APIENTRY _PFNCOMMD)(HAB, ULONG, MPARAM, PUPSSTATUS);
typedef _PFNCOMMD* PFNCOMMD;

//the command to fill a port options list
typedef int  (APIENTRY _PFNLIST)(HWND, int, char*);
typedef _PFNLIST* PFNLIST;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif
