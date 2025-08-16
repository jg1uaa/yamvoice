/*
 *   Copyright (c) 2019-2021 by Thomas A. Early N7TAE
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

#include <iostream>
#include <sndio.h>

#include "SettingsDlg.h"

class CSettingsDlgSndio
{
public:
	static bool isValid(std::string, bool);
	static std::string fltk_str(std::string);
};

bool CSettingsDlgSndio::isValid(std::string device, bool capture)
{
	struct sio_hdl *handle;
	bool hit, result = false;
	struct sio_cap cap;
	int i;

	if ((handle = sio_open(device.c_str(), capture ? SIO_REC : SIO_PLAY, 0)) == NULL)
		goto fin;

	if (!sio_getcap(handle, &cap))
		goto fin;

	// 16bit signed, native endian required
	for (i = 0, hit = false; i < SIO_NENC && !hit; i++) {
		hit = (cap.enc[i].bits == 16 &&
		       cap.enc[i].bps == SIO_BPS(16) &&
		       cap.enc[i].sig &&
#ifdef BIGENDIAN
		       !cap.enc[i].le
#else
		       cap.enc[i].le
#endif
		       );
	}
	if (!hit)
		goto fin;
		  
	// single channel (mono) required
	for (i = 0, hit = false; i < SIO_NCHAN && !hit; i++)
		hit = (capture ? cap.rchan[i] : cap.pchan[i]) == 1;
	if (!hit)
		goto fin;

	// sample rate check
	for (i = 0, hit = false; i < SIO_NRATE && !hit; i++) {
#ifdef USE44100
		hit = (cap.rate[i] == 44100);
#else
		hit = (cap.rate[i] == 8000);
#endif
	}
	if (!hit)
		goto fin;

	result = true;
fin:
	if (handle != NULL) sio_close(handle);
	return result;
}

std::string fltk_str(std::string str)
{
	std::string c, result = "";

	for (size_t i = 0; i < str.length(); i++) {
		c = str.substr(i, 1);
		if (c == "/") result += "\\";
		result += c;
	}

	return result;
}

void CSettingsDlg::AudioRescanButton()
{
	auto inchoice = pAudioInputChoice->value();
	if (inchoice < 0)
		inchoice = 0;
	auto outchoice = pAudioOutputChoice->value();
	if (outchoice < 0)
		outchoice = 0;
	pAudioInputChoice->clear();
	pAudioOutputChoice->clear();
	AudioInMap.clear();
	AudioOutMap.clear();

	std::string name;
	for (int i = -1; i < 20; i++) {
		if (i < 0)
			name = SIO_DEVANY; // "default"
		else if (i < 10)
			name = "snd/" + std::to_string(i % 10);
		else
			name = "rsnd/" + std::to_string(i % 10);

		if (CSettingsDlgSndio::isValid(name.c_str(), true)) {
			AudioInMap[name] = std::pair<std::string, std::string>(name, "");
			pAudioInputChoice->add(fltk_str(name).c_str());
		}
		if (CSettingsDlgSndio::isValid(name.c_str(), false)) {
			AudioOutMap[name] = std::pair<std::string, std::string>(name, "");
			pAudioOutputChoice->add(fltk_str(name).c_str());
		}
	}

	pAudioInputChoice->value(inchoice);
	AudioInputChoice();
	pAudioOutputChoice->value(outchoice);
	AudioOutputChoice();
}
