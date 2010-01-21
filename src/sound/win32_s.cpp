/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_s.cpp Handling of sound for Windows. */

#include "../stdafx.h"
#include "../openttd.h"
#include "../driver.h"
#include "../mixer.h"
#include "../core/alloc_func.hpp"
#include "../core/bitmath_func.hpp"
#include "win32_s.h"
#include <windows.h>
#include <mmsystem.h>

static FSoundDriver_Win32 iFSoundDriver_Win32;

static HWAVEOUT _waveout;
static WAVEHDR _wave_hdr[2];
static int _bufsize;

static void PrepareHeader(WAVEHDR *hdr)
{
	hdr->dwBufferLength = _bufsize * 4;
	hdr->dwFlags = 0;
	hdr->lpData = MallocT<char>(_bufsize * 4);
	if (waveOutPrepareHeader(_waveout, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
		usererror("waveOutPrepareHeader failed");
}

static void FillHeaders()
{
	WAVEHDR *hdr;

	for (hdr = _wave_hdr; hdr != endof(_wave_hdr); hdr++) {
		if (!(hdr->dwFlags & WHDR_INQUEUE)) {
			MxMixSamples(hdr->lpData, hdr->dwBufferLength / 4);
			if (waveOutWrite(_waveout, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
				usererror("waveOutWrite failed");
		}
	}
}

static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
	DWORD dwParam1, DWORD dwParam2)
{
	switch (uMsg) {
		case WOM_DONE:
			if (_waveout != NULL) FillHeaders();
			break;
		default: break;
	}
}

const char *SoundDriver_Win32::Start(const char * const *parm)
{
	WAVEFORMATEX wfex;
	wfex.wFormatTag = WAVE_FORMAT_PCM;
	wfex.nChannels = 2;
	wfex.wBitsPerSample = 16;
	wfex.nSamplesPerSec = GetDriverParamInt(parm, "hz", 44100);
	wfex.nBlockAlign = (wfex.nChannels * wfex.wBitsPerSample) / 8;
	wfex.nAvgBytesPerSec = wfex.nSamplesPerSec * wfex.nBlockAlign;

	_bufsize = GetDriverParamInt(parm, "bufsize", (GB(GetVersion(), 0, 8) > 5) ? 8192 : 4096);

	if (waveOutOpen(&_waveout, WAVE_MAPPER, &wfex, (DWORD_PTR)&waveOutProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
		return "waveOutOpen failed";

	MxInitialize(wfex.nSamplesPerSec);

	PrepareHeader(&_wave_hdr[0]);
	PrepareHeader(&_wave_hdr[1]);
	FillHeaders();
	return NULL;
}

void SoundDriver_Win32::Stop()
{
	HWAVEOUT waveout = _waveout;

	_waveout = NULL;
	waveOutReset(waveout);
	waveOutUnprepareHeader(waveout, &_wave_hdr[0], sizeof(WAVEHDR));
	waveOutUnprepareHeader(waveout, &_wave_hdr[1], sizeof(WAVEHDR));
	waveOutClose(waveout);
}
