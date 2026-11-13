#pragma once

#include <memory>
#include <string>

#include "hrtf/IHrtfDataSet.h"

// Runtime settings and resource lookup for the HRTF layer.
//
// Settings are read once from "<this dll's file name>.ini" located next to the dll
// (for example x3daudio_hrtf_core.ini or x3daudio1_7.ini). A missing file or a missing key
// simply keeps the default.
//
//   [Audio]
//   HrtfGain=1.0          ; extra gain for every spatialized (HRTF) sound. 0.5 reproduces the old behaviour for mono sources.
//   StereoSourceGain=1.0  ; additional gain only for stereo sources (they are averaged, not summed). 2.0 reproduces the old behaviour for stereo sources.
//   NonHrtfGain=1.0       ; gain applied to the 5.1 -> stereo fold-down used by sounds that are not spatialized (music, UI, 2D)
//   Crossfade=1           ; 1 = smooth filter/volume changes inside each audio buffer (fixes clicks), 0 = old behaviour
//   DelayInterpolation=2  ; how the (fractional) left/right ear delay is realised: 0 = whole samples (old behaviour),
//                         ; 1 = linear interpolation (slightly dulls the highest frequencies),
//                         ; 2 = 8-tap windowed sinc (within 0.1 dB up to 10 kHz and 1 dB up to 14 kHz at 44.1 kHz; adds a constant 3 sample = 68 us delay to both ears)
namespace hrtf_config
{
	struct Settings
	{
		float hrtf_gain = 1.0f;
		float stereo_source_gain = 1.0f;
		float non_hrtf_gain = 1.0f;
		bool crossfade = true;
		int delay_interpolation = 2;
	};

	const Settings & get();

	// Directory of this dll, with trailing backslash. Empty if it cannot be determined.
	std::wstring module_directory();

	// Loads (once) every *.mhr file from "<dll directory>\hrtf". Falls back to ".\hrtf" relative to the
	// current directory. Returns nullptr if nothing usable was found. The failure is remembered, so no
	// disk scan happens on every new voice.
	std::shared_ptr<IHrtfDataSet> get_shared_data_set();
}
