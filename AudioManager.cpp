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

#include <iostream>
#include <fstream>
#include <thread>

#include "MainWindow.h"
#include "AudioManager.h"
#include "Configure.h"
#include "codec2.h"
#include "Callsign.h"

CAudioManager::CAudioManager() : hot_mic(false), play_file(false), m17_sid_in(0U)
{
	link_open = true;
	volStats.count = 0;

#ifdef USE44100
	expand.data_in = expand_in;
	expand.data_out = expand_out;
	expand.input_frames = 160;
	expand.output_frames = 882;
	expand.end_of_input = false;
	RSExpand.SetRatio(expand, 44100.0/8000.0);

	shrink.data_in = shrink_in;
	shrink.data_out = expand_out;
	shrink.input_frames = 882;
	shrink.output_frames = 160;
	shrink.end_of_input = false;
	RSShrink.SetRatio(shrink, 8000.0/44100.0);
#endif
}

bool CAudioManager::Init(CMainWindow *pMain)
{
	pMainWindow = pMain;

	AM2M17.SetUp("am2m17");
	LogInput.SetUp("log_input");
	return false;
}


void CAudioManager::RecordMicThread(E_PTT_Type for_who, const std::string &urcall)
{
	// wait on audio queue to empty

	unsigned int count = 0u;
	while (! audio_queue.IsEmpty()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		count++;
	}
	if (count > 0)
		std::cout << "Tailgating detected! Waited " << count*20 << " ms for audio queue to clear." << std::endl;

	auto data = pMainWindow->cfg.GetData();
	hot_mic = true;

	mic2audio_fut = std::async(std::launch::async, &CAudioManager::mic2audio, this);

	audio2codec_fut = std::async(std::launch::async, &CAudioManager::audio2codec, this, data->bVoiceOnlyEnable);

	if (for_who == E_PTT_Type::m17) {
		codec2gateway_fut = std::async(std::launch::async, &CAudioManager::codec2gateway, this, urcall, data->sM17SourceCallsign, data->bVoiceOnlyEnable);
	}
}

void CAudioManager::audio2codec(const bool is_3200)
{
	CCodec2 c2(is_3200);
	bool last;
	calc_audio_stats();  // initialize volume statistics
	bool is_odd = false; // true if we've processed an odd number of audio frames
	do {
		// we'll wait until there is something
		CAudioFrame audioframe = audio_queue.WaitPop();
		calc_audio_stats(audioframe.GetData());
		last = audioframe.GetFlag();
		if ( is_3200 ) {
			is_odd = ! is_odd;
			unsigned char data[8];
			c2.codec2_encode(data, audioframe.GetData());
			CC2DataFrame dataframe(data);
			dataframe.SetFlag(is_odd ? false : last);
			c2_queue.Push(dataframe);
			if (is_odd && last) { // we need an even number of data frame for 3200
				// add one more quite frame
				const short quiet[160] = { 0 };
				c2.codec2_encode(data, quiet);
				CC2DataFrame frame2(data);
				frame2.SetFlag(true);
				c2_queue.Push(frame2);
			}
		} else { // 1600 - we need 40 ms of audio
			short audio[320] = { 0 }; // initialize to 40 ms of silence
			unsigned char data[8];
			memcpy(audio, audioframe.GetData(), 160*sizeof(short)); // we'll put 20 ms of audio at the beginning
			if (last) { // get another frame, if available
				volStats.count += 160; // a quite frame will only contribute to the total count
			} else {
				//we'll wait until there is something
				audioframe = audio_queue.WaitPop();
				calc_audio_stats(audioframe.GetData());
				memcpy(audio+160, audioframe.GetData(), 160*sizeof(short));	// now we have 40 ms total
				last = audioframe.GetFlag();
			}
			c2.codec2_encode(data, audio);
			CC2DataFrame dataframe(data);
			dataframe.SetFlag(last);
			c2_queue.Push(dataframe);
		}
	} while (! last);
}

void CAudioManager::QuickKey(const std::string &d, const std::string &s)
{
	hot_mic = true;
	SM17Frame frame;
	CCallsign dest(d), sour(s);
	memcpy(frame.magic, "M17 ", 4);
	frame.streamid = random.NewStreamID();
	dest.CodeOut(frame.lich.addr_dst);
	sour.CodeOut(frame.lich.addr_src);
	frame.SetFrameType(0x5u);
	memset(frame.lich.nonce, 0, 14);
	const uint8_t quiet[] = { 0x01u, 0x00u, 0x09u, 0x43u, 0x9cu, 0xe4u, 0x21u, 0x08u };
	memcpy(frame.payload,     quiet, 8);
	memcpy(frame.payload + 8, quiet, 8);
	for (uint16_t i=0; i<5; i++) {
		frame.SetFrameNumber((i < 4) ? i : i & 0x8000u);
		frame.SetCRC(crc.CalcCRC(frame));
		AM2M17.Write(frame.magic, sizeof(SM17Frame));
	}
	hot_mic = false;
}

void CAudioManager::codec2gateway(const std::string &dest, const std::string &sour, bool voiceonly)
{
	CCallsign destination(dest);
	CCallsign source(sour);

	// make most of the M17 IP frame
	// TODO: nonce and encryption and more TODOs mentioned later...
	SM17Frame ipframe;
	memcpy(ipframe.magic, "M17 ", 4);
	ipframe.streamid = random.NewStreamID(); // no need to htons because it's just a random id
	ipframe.SetFrameType(voiceonly ? 0x5U : 0x7U);
	destination.CodeOut(ipframe.lich.addr_dst);
	source.CodeOut(ipframe.lich.addr_src);

	unsigned int count = 0;
	bool last;
	do {
		// we'll wait until there is something
		CC2DataFrame cframe = c2_queue.WaitPop();
		last = cframe.GetFlag();
		memcpy(ipframe.payload, cframe.GetData(), 8);
		if (voiceonly) {
			if (last) {
				// we should never get here, but just in case...
				std::cerr << "WARNING: unexpected end of 3200 voice stream!" << std::endl;
				const uint8_t quiet[] = { 0x00u, 0x01u, 0x43u, 0x09u, 0xe4u, 0x9cu, 0x08u, 0x21u };	// for 3200 only!
				memcpy(ipframe.payload+8, quiet, 8);
			} else {
				// fill in the second part of the payload for C2 3200
				cframe = c2_queue.WaitPop();
				last = cframe.GetFlag();
				memcpy(ipframe.payload+8, cframe.GetData(), 8);
			}
		}
		// TODO: do something with the 2nd half of the payload when it's voice + "data"

		uint16_t fn = count++ % 0x8000u;
		if (last)
			fn |= 0x8000u;
		ipframe.SetFrameNumber(fn);

		// TODO: calculate crc

		AM2M17.Write(ipframe.magic, sizeof(SM17Frame));
	} while (! last);
}

void CAudioManager::codec2audio(const bool is_3200)
{
	CCodec2 c2(is_3200);
	bool last;
	calc_audio_stats(); // init volume stats
	do {
		// we'll wait until there is something
		CC2DataFrame dataframe = c2_queue.WaitPop();
		last = dataframe.GetFlag();
		if (is_3200) {
			short audio[160];
			c2.codec2_decode(audio, dataframe.GetData());
			CAudioFrame audioframe(audio);
			audioframe.SetFlag(last);
			audio_queue.Push(audioframe);
			calc_audio_stats(audio);
		} else {
			short audio[320];	// C2 1600 is 40 ms audio
			c2.codec2_decode(audio, dataframe.GetData());
			CAudioFrame audio1(audio), audio2(audio+160);
			audio1.SetFlag(false);
			audio2.SetFlag(last);
			audio_queue.Push(audio1);
			audio_queue.Push(audio2);
			calc_audio_stats(audio);
			calc_audio_stats(audio+160);
		}
	} while (! last);
}

void CAudioManager::PlayEchoDataThread()
{
	auto data = pMainWindow->cfg.GetData();
	hot_mic = false;
	mic2audio_fut.get();
	audio2codec_fut.get();

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	codec2audio_fut = std::async(std::launch::async, &CAudioManager::codec2audio, this, data->bVoiceOnlyEnable);
	play_audio_fut = std::async(std::launch::async, &CAudioManager::play_audio, this);
	codec2audio_fut.get();
	play_audio_fut.get();
}

void CAudioManager::M17_2AudioMgr(const SM17Frame &m17)
{
	static bool is_3200;
	if (! play_file) {
		if (0U==m17_sid_in && 0U==(m17.GetFrameNumber() & 0x8000u)) {	// don't start if it's the last audio frame
			// here comes a new stream
			m17_sid_in = m17.streamid;
			is_3200 = ((m17.GetFrameType() & 0x6u) == 0x4u);
			pMainWindow->Receive(true);
			// launch the audio processing threads
			codec2audio_fut = std::async(std::launch::async, &CAudioManager::codec2audio, this, is_3200);
			play_audio_fut = std::async(std::launch::async, &CAudioManager::play_audio, this);
		}
		if (m17.streamid != m17_sid_in)
			return;
		auto payload = m17.payload;
		CC2DataFrame dataframe(payload);
		auto last = (0x8000u == (m17.GetFrameNumber() & 0x8000u));
		dataframe.SetFlag(is_3200 ? false : last);
		c2_queue.Push(dataframe);
		if (is_3200) {
			CC2DataFrame frame2(payload+8);
			frame2.SetFlag(last);
			c2_queue.Push(frame2);
		}
		if (last) {
			codec2audio_fut.get();	// we're done, get the finished threads and reset the current stream id
			play_audio_fut.get();
			m17_sid_in = 0U;
			pMainWindow->Receive(false);
		}
	}
}

void CAudioManager::KeyOff()
{
	if (hot_mic) {
		hot_mic = false;
		mic2audio_fut.get();
		audio2codec_fut.get();
		codec2gateway_fut.get();
	}
}

void CAudioManager::Link(const std::string &linkcmd)
{
	if (0 == linkcmd.compare(0, 3, "M17")) { //it's an M17 link/unlink command
		SM17Frame frame;
		if ('L' == linkcmd.at(3)) {
			if (13 == linkcmd.size()) {
				std::string sDest(linkcmd.substr(4));
				sDest[7] = 'L';
				CCallsign dest(sDest);
				dest.CodeOut(frame.lich.addr_dst);
				//printf("dest=%s=0x%02x%02x%02x%02x%02x%02x\n", dest.GetCS().c_str(), frame.lich.addr_dst[0], frame.lich.addr_dst[1], frame.lich.addr_dst[2], frame.lich.addr_dst[3], frame.lich.addr_dst[4], frame.lich.addr_dst[5]);
				AM2M17.Write(frame.magic, sizeof(SM17Frame));
			}
		} else if ('U' == linkcmd.at(3)) {
			CCallsign dest("U");
			dest.CodeOut(frame.lich.addr_dst);
			AM2M17.Write(frame.magic, sizeof(SM17Frame));
		}
	}
}

void CAudioManager::calc_audio_stats(const short int *wave)
{
	if (wave) {
		double ss = 0.0;
		for (unsigned int i=0; i<160; i++) {
			auto a = (unsigned int)abs(wave[i]);
			if (a > 16383) volStats.clip++;
			if (i % 2) ss += double(a) * double(a); // every other point will do
		}
		volStats.count += 160;
		volStats.ss += ss;
	} else {
		volStats.count = volStats.clip = 0;
		volStats.ss = 0.0;
	}
}
