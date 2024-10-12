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

#include <pulse/simple.h>

#include <iostream>
#include <thread>

#include "MainWindow.h"
#include "AudioManager.h"

#ifdef USE44100
#error USE44100 not supported
#endif

void CAudioManager::mic2audio()
{
	auto data = pMainWindow->cfg.GetData();
	std::string device = data->sAudioIn;
	pa_sample_spec ss = {
#ifdef BIGENDIAN
		.format = PA_SAMPLE_S16BE,
#else
		.format = PA_SAMPLE_S16LE,
#endif
		.rate = 8000,
		.channels = 1,
	};
	pa_simple *handle = pa_simple_new(NULL, "yamvoice", PA_STREAM_RECORD, (device == "default") ? NULL : device.c_str(), "mic2audio", &ss, NULL, NULL, NULL);

	if (handle == NULL) {
		std::cerr << "unable to open pcm device: " << device << std::endl;
		return;
	}

	bool keep_running;
	do {
		short audio_buffer[160];
		int err;

		if (pa_simple_read(handle, audio_buffer, sizeof(audio_buffer), &err) < 0)
			std::cerr << "pa_simple_read error: " << err << std::endl;

		keep_running = hot_mic;

		CAudioFrame frame(audio_buffer);
		frame.SetFlag(! keep_running);
		audio_queue.Push(frame);
	} while (keep_running);

	pa_simple_free(handle);
}

void CAudioManager::play_audio()
{
	auto data = pMainWindow->cfg.GetData();
	std::string device = data->sAudioOut;
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	pa_sample_spec ss = {
#ifdef BIGENDIAN
		.format = PA_SAMPLE_S16BE,
#else
		.format = PA_SAMPLE_S16LE,
#endif
		.rate = 8000,
		.channels = 1,
	};
	pa_simple *handle = pa_simple_new(NULL, "yamvoice", PA_STREAM_PLAYBACK, (device == "default") ? NULL : device.c_str(), "play_audio", &ss, NULL, NULL, NULL);

	if (handle == NULL) {
		std::cerr << "unable to open pcm device: " << device << std::endl;
		return;
	}

	bool last;
	do {
		CAudioFrame frame(audio_queue.WaitPop());	// wait for a packet
		last = frame.GetFlag();
		int err;
		if (pa_simple_write(handle, frame.GetData(), frame.Size() * sizeof(short), &err) < 0)
			std::cerr << "pa_simple_write error: " << err << std::endl;
	} while (! last);

	pa_simple_free(handle);
}
