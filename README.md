# X3DAudio HRTF Alma Edition

True HRTF (binaural 3D) audio for **F.E.A.R. 2: Project Origin**, built on the X3DAudio HRTF mod. Other games that use X3DAudio + XAudio2 may work with the original mod, but this edition has only been tested with F.E.A.R. 2.

Demo videos of the original mod: [Skyrim](https://www.youtube.com/watch?v=jJPpqssMPl0), [more](http://www.youtube.com/watch?v=hsCgaMeTVG0)

## What's different from the original mod

- No more clicks when turning fast (filters and volume change smoothly)
- Exact left/right ear delay instead of rounding to whole samples (selectable quality, see `DelayInterpolation`)
- Sound tails (echoes, reverb) are no longer cut off
- Mono 3D sounds are no longer 6 dB too quiet, and non-HRTF sounds (music, UI) are no longer quieter
- The `hrtf` folder is found next to the DLL, whatever the current directory is
- Fixes for memory leaks, stale audio and other issues, plus an optional settings file

Details: [CHANGELOG.md](CHANGELOG.md) and [patches/README_PATCHES.txt](patches/README_PATCHES.txt).

## Download

Get the latest zip from the [Releases page](https://github.com/SludgeVisor/X3DAudioHRTF-Alma-Edition/releases).

## Installation (F.E.A.R. 2)

You need the 32-bit **Microsoft Visual C++ 2015-2022 Redistributable (x86)** and the **DirectX End-User Runtime (June 2010)**. The game's own installer usually installs the second one.

1. Unzip the release into the folder that contains `FEAR2.exe`, so you have:
   - `X3DAudio1_5.dll` (shim, lets the game use the HRTF core)
   - `x3daudio_hrtf_core.dll` (the HRTF core)
   - `hrtf\` (HRTF data, `.mhr` files)
   - `x3daudio_hrtf_core.ini` (optional settings)
2. Use stereo or headphone output in Windows, and start the game.

To uninstall, delete those files.

Some anti-cheat software (for example BattleEye) blocks hooks like this, so there is no HRTF in multiplayer with it enabled.

## Settings

All settings are optional and go in `x3daudio_hrtf_core.ini` next to the DLL. The file is read once at startup, so restart the game after editing. See `patches/x3daudio_hrtf.ini.sample` for all options with explanations.

| Setting | Default | What it does |
|---|---|---|
| `HrtfGain` | 1.0 | Extra gain for 3D (HRTF) sounds. 0.5 is the old level. |
| `StereoSourceGain` | 1.0 | Extra gain for stereo sources only. |
| `NonHrtfGain` | 1.0 | Gain for sounds that are not spatialized (music, UI, 2D). |
| `Crossfade` | 1 | 1 = smooth changes (no clicks), 0 = old behaviour. |
| `DelayInterpolation` | 2 | Ear delay quality, 0 to 5. Higher values are more accurate above about 12 kHz and add a tiny constant delay. 4 is a good step up. |

If loud scenes distort, lower `HrtfGain` and `NonHrtfGain` (for example 0.7).

## HRTF data

The `.mhr` files in `hrtf\` are chosen by the sample rate stored inside each file, not by their file name. The game uses sample rates of 44100, 32000, 22050, 48000 and 24000 Hz, so those should be present. A missing rate falls back to the closest one and is reported in `log.txt`. Do not put two sets with the same sample rates in the same folder.

The bundled data comes from IRCAM, see [IRCAM LISTEN HRTF Database](#ircam-listen-hrtf-database) below and [hrtf/CREDITS.md](hrtf/CREDITS.md).

## Troubleshooting

- The core writes `log.txt` (next to the game's working directory). A healthy start shows `Loaded HRTF data from ...` and, if you use the ini, `Settings from ...`.
- To log the shim too, create an empty file called `X3DAudio1_5_shim.debug` in the game folder and read `X3DAudio1_5_shim.log` after a run.

## How it works

The core hooks `x3daudio1_x.dll` and the `IXAudio2` COM interface. When the game positions a sound with X3DAudio, the position is remembered. When the game then sets the sound's output matrix, the sound is routed through its own submix voice that carries an HRTF effect (an XAPO) fed with that position.

F.E.A.R. 2 was built for XAudio2 2.3, while the core provides XAudio2 2.7 objects, and the two differ in their binary layout. `X3DAudio1_5.dll` is a small shim that forwards the X3DAudio calls to the core and presents XAudio2 objects in the layout the game expects. Its source is in [shim_source/](shim_source/).

## Credits and legal

- Original X3DAudio HRTF by kosumosu: https://github.com/kosumosu/x3daudio1_7_hrtf
- Fork and HRIR Batch Converter by ThreeDeeJay: https://github.com/ThreeDeeJay/X3DAudioHRTF
- **AI disclosure:** all modifications in this Alma Edition were made by Claude (Anthropic), https://claude.ai
- The mod is licensed under the **GPLv3**, see [LICENSE](LICENSE).

### Mhook

Uses [Mhook](http://codefromthe70s.org/mhook24.aspx).

```
Copyright (c) 2007-2012, Marton Anka
Portions Copyright (c) 2007, Matt Conover

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### IRCAM LISTEN HRTF Database

**Applies to:** `hrtf/*.mhr` (HRTF data derived from subject IRC_1037 of the IRCAM LISTEN HRTF Database).

The data files are derived from the *Listen HRTF Database*, recorded in the IRCAM anechoic room (Paris, France) in 2002-2003. Principal investigator: Olivier Warusfel, Room Acoustics Team, IRCAM. The data was converted to the `.mhr` format used by this mod. The original files are distributed in SOFA and IRCAM native formats:

- [Original project page (archived)](http://recherche.ircam.fr/equipes/salles/listen/)
- [SOFA-format mirror](https://sofacoustics.org/data/database/listen%20%28hrtf%29/) (contains the `IRC_1037_*` subject files)
- [Catalogue entry (SONICOM Ecosystem, Database #78)](https://ecosystem.sonicom.eu/databases/78), which lists the database as [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)

IRCAM's notice:

```
Copyright (c) 2002 IRCAM (Institut de Recherche et Coordination
Acoustique/Musique). All Rights Reserved.

Use of Materials:

IRCAM hereby grants users permission to use materials available therein
for any purpose - educational, research or commercial. However, each
reproduction of any part of the materials must include the copyright
notice, if it is present. In addition, as a courtesy, if these materials
are used in published research, this use should be acknowledged in the
publication as follows:

UMR 9912 - STMS - IRCAM/CNRS/UPMC, 1, place Stravinsky 75004 PARIS

Disclaimer:

IRCAM MAKES NO REPRESENTATION OR WARRANTIES WITH RESPECT TO THE CONTENTS
HEREOF AND SPECIFICALLY DISCLAIM ANY IMPLIED WARRANTIES OF MERCHANTABILITY
OR FITNESS FOR ANY PARTICULAR PURPOSE. Further, IRCAM reserves the right
to revise this database and/or documentation and to make changes from time
to time in the content hereof without obligation of IRCAM to notify any
person of such revision or change.
```

IRCAM requests that use of the database be acknowledged. If you use or redistribute this project, please keep this notice and the credit above.

Contact for the original dataset: hrtf@ircam.fr
