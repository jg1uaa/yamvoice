/*
 *   Copyright (c) 2019-2020 by Thomas A. Early N7TAE
 *   Copyright (c) 2024 by SASANO Takayoshi JG1UAA
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sndio.h>

#include <iostream>
#include <thread>

#include "MainWindow.h"
#include "AudioManager.h"

#define byte2frame(x, y) ((x) / sizeof(y))
#define frame2byte(x, y) ((x) * sizeof(y))

class CAudioManagerSndio
{
public:
	static struct sio_hdl* open(std::string, bool, int, int);
};

struct sio_hdl* CAudioManagerSndio::open(std::string device, bool capture, int rate, int frames)
{
	struct sio_hdl *handle;
	if ((handle = sio_open(device.c_str(), capture ? SIO_REC : SIO_PLAY, 0)) == NULL) {
		std::cerr << "unable to open pcm device: " << device << std::endl;
		goto fail;
	}

	struct sio_par q, r;
	sio_initpar(&q);
	q.bits = 16;
	q.bps = SIO_BPS(q.bits);
	q.sig = 1;
	q.le = SIO_LE_NATIVE;
	q.msb = 0;
	q.rchan = q.pchan = 1;
	q.rate = rate;
	q.xrun = SIO_IGNORE;
	q.appbufsz = frames * q.bps;

	if (!sio_setpar(handle, &q) || !sio_getpar(handle, &r) ||
	    q.bits != r.bits || q.bps != r.bps || q.sig != r.sig ||
	    q.le != r.le || q.msb != r.msb ||
	    (capture && q.rchan != r.rchan) ||
	    (!capture && q.pchan != r.pchan) ||
	    q.rate != r.rate || q.xrun != r.xrun || q.appbufsz != r.appbufsz) {
		std::cerr << "unable to set hw parameters: " << device << std::endl;
		goto fail;
	}

	return handle;

fail:
	if (handle != NULL) sio_close(handle);
	return NULL;
}

void CAudioManager::mic2audio()
{
	auto data = pMainWindow->cfg.GetData();
	// Open PCM device for recording (capture).
	struct sio_hdl *handle;
	unsigned int frames;
#ifdef USE44100
	handle = CAudioManagerSndio::open(data->sAudioIn, true, 44100, frames = shrink.input_frames);
#else	
	handle = CAudioManagerSndio::open(data->sAudioIn, true, 8000, frames = 160);
#endif
	if (handle == NULL || !sio_start(handle))
		return;

	bool keep_running;
	do {
		short audio_buffer[frames];
#ifdef USE44100
		short audio_frame[160];
#endif
		unsigned int n, pos, remain;
		for (pos = 0; pos < frames; pos += byte2frame(n, short)) {
			remain = frames - pos;
			n = sio_read(handle, audio_buffer + pos, frame2byte(remain, short));
		}
		keep_running = hot_mic;
#ifdef USE44100
		RSShrink.Short2Float(audio_buffer, shrink.data_in, shrink.input_frames);
		if (RSShrink.Process(shrink))
			keep_running = hot_mic = false;
		RSShrink.Float2Short(shrink.data_out, audio_frame, shrink.output_frames);
		CAudioFrame frame(audio_frame);
#else
		CAudioFrame frame(audio_buffer);
#endif
		frame.SetFlag(! keep_running);
		audio_queue.Push(frame);
	} while (keep_running);
#ifdef USE44100
	RSShrink.Reset();
#endif
	sio_stop(handle);
	sio_close(handle);
}

void CAudioManager::play_audio()
{
	auto data = pMainWindow->cfg.GetData();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	// Open PCM device for playback.
	struct sio_hdl *handle;
	unsigned int frames;
#ifdef USE44100
	handle = CAudioManagerSndio::open(data->sAudioOut, false, 44100, frames = expand.input_frames);
#else	
	handle = CAudioManagerSndio::open(data->sAudioOut, false, 8000, frames = 160);
#endif
	if (handle == NULL || !sio_start(handle))
		return;

	bool last;
	do {
#ifdef USE44100
		short short_out[882];
#endif
		CAudioFrame frame(audio_queue.WaitPop());	// wait for a packet
		last = frame.GetFlag();
#ifdef USE44100
		RSExpand.Short2Float(frame.GetData(), expand.data_in, expand.input_frames);
		if (RSExpand.Process(expand))
			last = true;
		RSExpand.Float2Short(expand.data_out, short_out, expand.output_frames);
		sio_write(handle, short_out, frame2byte(expand.output_frames, short));
#else
		sio_write(handle, frame.GetData(), frame2byte(frames, short));
#endif
	} while (! last);

	sio_stop(handle);
	sio_close(handle);
#ifdef USE44100
	RSExpand.Reset();
#endif
}
