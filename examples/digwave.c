#define INCL_PM
#define INCL_DOSPROCESS
#include <stdio.h>
#include <os2.h>
#include "dive.h"
#include "audio.h"

FILE *fp;

#define FOURCC_R565 0x35363552ul
#define FOURCC_LUT8 0x3854554cul
#define FOURCC_SCRN 0
#define WM_VRNENABLE 0x7f
#define WM_VRNDISABLE 0x7e

#define WM_FORCEDPAINT WM_USER

/*extern "C" */ULONG APIENTRY WinSetVisibleRegionNotify( HWND win, BOOL bool );
/*extern "C" */ULONG APIENTRY WinQueryVisibleRegion( HWND win, HRGN hrgn );

ULONG blitdepth;
PBYTE imagebuf = NULL;
HWND frame, client;
char blitok = 0;
char paintpending = 0;

MRESULT EXPENTRY DigWave( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 ) {
	static HDIVE diveinst;
	static PVOID framebuffer = NULL;
	static ULONG linebytes, linetot, i, j, bufnum;
	ULONG err;
	RGNRECT rgnCtl;
	HPS hps;
	RECTL rectl;
	RECTL rcls[50];
	HRGN hrgn;
	SWP swp;
	POINTL pointl;
	static SETUP_BLITTER BlSet;
	DIVE_CAPS cap;
	char buffer[512];

	switch ( msg ) {
		case WM_CREATE:
			err = DiveOpen( &diveinst, FALSE, framebuffer );
			if ( err ) {
				return (MRESULT)-1;
			}
			err = DiveAllocImageBuffer( diveinst, &bufnum, FOURCC_R565,
			   300, 100, 600/((blitdepth==FOURCC_R565)?1:2), NULL );
			if ( err ) {
				DiveClose( diveinst );
				return (MRESULT)-1;
			}
			err = DiveBeginImageBufferAccess( diveinst, bufnum, &imagebuf,
			    &linebytes, &linetot );
			if ( err ) {
				DiveFreeImageBuffer( diveinst, bufnum );
				DiveClose( diveinst );
				return (MRESULT)-1;
			}
			if ( blitdepth == FOURCC_R565 ) {
			unsigned short *tmp; 
			tmp = (unsigned short *)imagebuf;
			for ( i = 0; i < linetot; ++i ) {
				for ( j=0; j < linebytes; j += 2 ) {
					*tmp = 0;
					tmp++;
				}
			} 
			} else {
				for ( i = 0; i < linetot; ++i ) {
					for ( j=0; j < linebytes; j++ ) {
						*(imagebuf+(i*linebytes)+j) = 0;
					}
				}
			}
			WinStartTimer( WinQueryAnchorBlock(win), win, 0, 32 );
		break;
		case WM_OPEN:
			WinPostMsg( win, WM_VRNENABLE, 0, 0 );
		break;
		case WM_TIMER:
			if ( paintpending ) return;
			paintpending = 1;
			WinPostMsg( win, WM_FORCEDPAINT, 0, 0 );
		break;
		case WM_VRNDISABLE:
			DiveSetupBlitter(diveinst, NULL);
			blitok = 0;
		break;
		case WM_VRNENABLE:
			blitok = 0;
			hps=WinGetPS(win);
			if ( hps == 0 ) { err=1; break; }
			hrgn=GpiCreateRegion(hps, 0L, NULL);
			if (hrgn) {
				DosEnterCritSec();
				err = WinQueryVisibleRegion(win, hrgn);
				rgnCtl.ircStart=0;
				rgnCtl.crc=50;
				rgnCtl.ulDirection=RECTDIR_LFRT_TOPBOT;
				err = GpiQueryRegionRects(hps, hrgn, NULL, &rgnCtl, rcls);
				if ( err ) { 
					err = 0; 
					WinQueryWindowPos(win, &swp);
					pointl.x=swp.x;
					pointl.y=swp.y;
					WinMapWindowPoints(WinQueryWindow( win, QW_PARENT ), HWND_DESKTOP, (POINTL*)&pointl, 1);
					BlSet.ulStructLen = sizeof( SETUP_BLITTER );
					BlSet.fInvert = 0;
					BlSet.fccSrcColorFormat = blitdepth;
					BlSet.ulSrcWidth = 300;
					BlSet.ulSrcHeight = 100;
					BlSet.ulSrcPosX = 0;
					BlSet.ulSrcPosY = 0;
					BlSet.ulDitherType = 1;
					BlSet.fccDstColorFormat = FOURCC_SCRN;
					BlSet.ulDstWidth=swp.cx;
					BlSet.ulDstHeight=swp.cy;
					BlSet.lDstPosX = 0;
					BlSet.lDstPosY = 0;
					BlSet.lScreenPosX=pointl.x;
					BlSet.lScreenPosY=pointl.y;
					BlSet.ulNumDstRects=rgnCtl.crcReturned;
					BlSet.pVisDstRects=rcls;
					if ( rgnCtl.crcReturned == 0 ) { 
						DiveSetupBlitter( diveinst, NULL );
						DosExitCritSec();
						GpiDestroyRegion(hps, hrgn);
						return (MRESULT)-1;
					}
					err = DiveSetupBlitter(diveinst, &BlSet);
					blitok = 1;
					DosExitCritSec();
					if ( err ) { return (MRESULT)-1;}
					GpiDestroyRegion(hps, hrgn);
				}
			}
			WinReleasePS( hps );
		break;
		case WM_QUIT:
		case WM_CLOSE:
			WinStopTimer( WinQueryAnchorBlock(win), win, 0 );
			blitok = 0;
			DiveSetupBlitter(diveinst, NULL);
			DosEnterCritSec();
			err = DiveEndImageBufferAccess( diveinst, bufnum );
			imagebuf = NULL;
			DosExitCritSec();
			if ( err ) {
				DiveFreeImageBuffer( diveinst, bufnum );
				DiveClose( diveinst );
				break;
			}
			DiveFreeImageBuffer( diveinst, bufnum );
			bufnum = 0;
			DiveClose( diveinst );
			diveinst = 0;
		break;
		case WM_FORCEDPAINT:
			// intentional fall-through to WM_PAINT
			paintpending = 0;
		case WM_PAINT:
			if ( blitok ) DiveBlitImage( diveinst, bufnum, DIVE_BUFFER_SCREEN );
	}
	return WinDefWindowProc( win, msg, mp1, mp2 );
}

void DigWaveFilt( unsigned char *data, unsigned long len ) {
	static unsigned long oldk_left = 50*600, oldk_right = 50*600; // Initial value is in the middle
	register unsigned long i, j, k, hi, lo;
	if ( !imagebuf ) return;
	if ( paintpending ) return;
	memset( imagebuf, 0, 100*300*2 );
	for (i=0; i<len; i+=2) {  // 2 for 8 bit stereo ! :)
		if ( ((((unsigned char)(((data[i]))) * 100) / 256)*600) > oldk_left ) {
			hi = (((unsigned char)(((data[i]))) * 100) / 256)*600;
			lo = oldk_left;
			oldk_left = hi;
		} else {
			hi = oldk_left;
			lo = (((unsigned char)(((data[i]))) * 100) / 256)*600;
			oldk_left = lo;
		}

		for (k=lo; k<=hi; k+=600) {
			j = k + ((i*600)/len);
			if ( j % 2 ) j++;
			*(imagebuf + j) = 0x00;
			*(imagebuf + j + 1) = 0xd0;
		}

		if ( ((((unsigned char)(((data[i+1]))) * 100) / 256)*600) > oldk_right ) {
			hi = (((unsigned char)(((data[i+1]))) * 100) / 256)*600;
			lo = oldk_right;
			oldk_right = hi;
		} else {
			hi = oldk_right;
			lo = (((unsigned char)(((data[i+1]))) * 100) / 256)*600;
			oldk_right = lo;
		}

		for (k=lo; k<=hi; k+=600) {
			j = k + ((i*600)/len);
			if ( j % 2 ) j++;
			*(imagebuf + j) = 0x1f;
			*(imagebuf + j + 1) = 0x00;
		}

	}
}

int main( int argc, char *argv[] ) {
	ULONG rc;
	HAB ab;
	HMQ messq;
	ULONG frameflgs= FCF_TITLEBAR | FCF_TASKLIST | FCF_SYSMENU | FCF_SIZEBORDER | FCF_MINMAX;
	QMSG qmsg;
	AUDIOINFO info;
	LPAUDIOMODULE lpModule;

	if ( argc != 2 && argc != 3 ) return 1;

	DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0 );

	ab = WinInitialize( 0 );

	messq = WinCreateMsgQueue( ab, 0 );
	WinRegisterClass( ab, "SEAL Digital Wave Plugin", DigWave, 
	     CS_SIZEREDRAW | CS_MOVENOTIFY, 0 );

	blitdepth = FOURCC_R565;

	frame = WinCreateStdWindow( HWND_DESKTOP, WS_ANIMATE,
	    &frameflgs, "SEAL Digital Wave Plugin", "SEAL Digital Wave Plugin",
	    0, 0, 1, &client );

	WinSetWindowPos( frame, 0, 0, 0, 350, 120, SWP_SIZE | SWP_MOVE );
	WinShowWindow( frame, TRUE );
	WinFocusChange( HWND_DESKTOP, frame, 0 );
	WinSetVisibleRegionNotify( client, TRUE );
	WinPostMsg(frame, WM_VRNENABLE, 0L, 0L);

	/* initialize audio library */
	AInitialize();

	/* open audio device */
	info.nDeviceId = AUDIO_DEVICE_MAPPER;
	if ( (argc > 2) && (stricmp( argv[2], "-mmpm" ) == 0) ) {
		info.nDeviceId = 1;
	}
	info.wFormat = AUDIO_FORMAT_8BITS | AUDIO_FORMAT_STEREO;
	info.nSampleRate = 44100;

	ASuggestBufferSize( 4096 );
	rc = AOpenAudio(&info);
	if ( rc ) {
		return 2;
	} /* endif */

	/* load module file */
	rc = ALoadModuleFile( argv[1], &lpModule, 0);
	if ( rc ) {
		return 3;
	} /* endif */

	/* open voices and play module */
	AOpenVoices(lpModule->nTracks);
	APlayModule(lpModule);
	ARegisterFilter( &DigWaveFilt );

	while (WinGetMsg (ab, &qmsg, NULLHANDLE, 0, 0))
	    WinDispatchMsg (ab, &qmsg) ;

	/* stop module and close voices */
	AStopModule();
	ACloseVoices();

	/* release module file */
	AFreeModuleFile(lpModule);

	/* close audio device */
	ACloseAudio();

	WinDestroyWindow (frame) ;

	WinDestroyMsgQueue (messq) ;

	WinTerminate( ab );

	return 0;
}
