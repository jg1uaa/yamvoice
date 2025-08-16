/*
 *   Copyright (c) 2019-2020 by Thomas A. Early N7TAE
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

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include <netinet/in.h>

#include <iostream>
#include <thread>

#include "MainWindow.h"
#include "AudioManager.h"

void CAudioManager::mic2audio()
{
	auto data = pMainWindow->cfg.GetData();
	// Open PCM device for recording (capture).
	snd_pcm_t *handle;
	int rc = snd_pcm_open(&handle, data->sAudioIn.c_str(), SND_PCM_STREAM_CAPTURE, 0);
	if (rc < 0) {
		std::cerr << "unable to open pcm device: " << snd_strerror(rc) << std::endl;
		return;
	}
	// Allocate a hardware parameters object.
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);

	// Fill it in with default values.
	snd_pcm_hw_params_any(handle, params);

	// Set the desired hardware parameters.

	// Interleaved mode
	snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	// Signed 16-bit little-endian format
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

	// One channels (mono)
	snd_pcm_hw_params_set_channels(handle, params, 1);

#ifdef USE44100
	// 44100 samples/second
	snd_pcm_hw_params_set_rate(handle, params, 44100, 0);
	snd_pcm_uframes_t frames = shrink.input_frames;
#else
	// 8000 samples/second
	snd_pcm_hw_params_set_rate(handle, params, 8000, 0);
	snd_pcm_uframes_t frames = 160;
#endif
	snd_pcm_hw_params_set_period_size(handle, params, frames, 0);

	// Write the parameters to the driver
	rc = snd_pcm_hw_params(handle, params);
	if (rc < 0) {
		std::cerr << "unable to set hw parameters: " << snd_strerror(rc) << std::endl;
		return;
	}

	bool keep_running;
	do {
		short int audio_buffer[frames];
#ifdef USE44100
		short int audio_frame[160];
#endif
		rc = snd_pcm_readi(handle, audio_buffer, frames);
		if (rc == -EPIPE) {
			// EPIPE means overrun
			std::cerr << "overrun occurred" << std::endl;
			snd_pcm_prepare(handle);
		} else if (rc < 0) {
			std::cerr << "error from readi: " << snd_strerror(rc) << std::endl;
#ifdef USE44100
		} else if (rc != int(shrink.input_frames)) {
#else
		} else if (rc != int(frames)) {
#endif
			std::cerr << "short readi, read " << rc << " frames" << std::endl;
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
	snd_pcm_drop(handle);
	snd_pcm_close(handle);
}

void CAudioManager::play_audio()
{
	auto data = pMainWindow->cfg.GetData();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	// Open PCM device for playback.
	snd_pcm_t *handle;
	int rc = snd_pcm_open(&handle, data->sAudioOut.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0) {
		std::cerr << "unable to open pcm device: " << snd_strerror(rc) << std::endl;
		return;
	}

	// Allocate a hardware parameters object.
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(handle, params);

	// Set the desired hardware parameters.

	// Interleaved mode
	snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	// Signed 16-bit little-endian format
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

	// One channels (mono)
	snd_pcm_hw_params_set_channels(handle, params, 1);

#ifdef USE44100
	// samples/second sampling rate
	snd_pcm_hw_params_set_rate(handle, params, 44100, 0);
	// Set period size to 160 frames.
	snd_pcm_uframes_t frames = expand.input_frames;
#else
	// samples/second sampling rate
	snd_pcm_hw_params_set_rate(handle, params, 8000, 0);
	// Set period size to 160 frames.
	snd_pcm_uframes_t frames = 160;
#endif
	snd_pcm_hw_params_set_period_size(handle, params, frames, 0);
	//snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

	// Write the parameters to the driver
	rc = snd_pcm_hw_params(handle, params);
	if (rc < 0) {
		std::cerr << "unable to set hw parameters: " << snd_strerror(rc) << std::endl;
		return;
	}

	// Use a buffer large enough to hold one period
	snd_pcm_hw_params_get_period_size(params, &frames, 0);

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
		rc = snd_pcm_writei(handle, short_out, expand.output_frames);
#else
		rc = snd_pcm_writei(handle, frame.GetData(), frames);
#endif
		if (rc == -EPIPE) {
			// EPIPE means underrun
			// std::cerr << "underrun occurred" << std::endl;
			snd_pcm_prepare(handle);
		} else if (rc < 0) {
			std::cerr <<  "error from writei: " << snd_strerror(rc) << std::endl;
#ifdef USE44100
		}  else if (rc != int(expand.output_frames)) {
#else
		}  else if (rc != int(frames)) {
#endif
			std::cerr << "short write, wrote " << rc << " frames" << std::endl;
		}
	} while (! last);

	snd_pcm_drain(handle);
	snd_pcm_close(handle);
#ifdef USE44100
	RSExpand.Reset();
#endif
}
