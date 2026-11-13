# X3DAudio HRTF Alma Edition
## HRTF for 𝐅.𝐄.𝐀.𝐑. 𝟐: 𝐏𝐫𝐨𝐣𝐞𝐜𝐭 𝐎𝐫𝐢𝐠𝐢𝐧 and potentially other titles using X3dAudio + XAudio2 (including ones running on Unreal Engine 4 and UDK).

[![Youtube video](https://s15.postimg.org/slkiotxp7/skyrim.png)](https://www.youtube.com/watch?v=jJPpqssMPl0)
[![Youtube video](http://s24.postimg.org/iozt6p9ut/skyrim_fake_youtube.png)](http://www.youtube.com/watch?v=hsCgaMeTVG0)

### Tags
HRTF, HRIR, Binaural sound, Spatial sound, Ambisonics

### Download

Visit [releases page](https://github.com/kosumosu/x3daudio1_7_hrtf/releases) and grab the latest one. Ensure the version you download matches version of the game (x86 or x64).

### Installation
Unpack the contents of the package to the game folder where the main executable is located. Ensure it is the exe of the game itself, not a launcher. If your game uses prior version of x3daudio (earlier than 1.7), you can try to rename the dll to the one the game uses. Run the game. Enjoy!
Note: some anti-cheat software (e.g. BattleEye) won't let you hook anything into the game, so you will have to disable it. That means no HRTF in multiplayer.

#### Notes for developers
##### How it works
Game hooks both x3daudio1_x.dll, which is simple, and IXAudio2 COM interface, which is a less trivial thing to do.
X3daudio proxy remembers 3D audio position into repository. To mark sound as 3D it sets the first coefficient of the mixing matrix to NaN. And the second one is set to sound ID, so we can later identify it. Let's hope game is not that stupid and won't pass a 1x1 matrix.
Then, when game calls IXAuido2Voice::SetOutputMatrix(), we check if 1st coeff == NaN which means we have to enable our XAPO DSP effect and pass parameters to it (position, etc).

##### 𝐅.𝐄.𝐀.𝐑. 𝟐: 𝐏𝐫𝐨𝐣𝐞𝐜𝐭 𝐎𝐫𝐢𝐠𝐢𝐧
So far with testing it seems to work great

### Legal Stuff
#### X3DAudio HRTF
The given library is licensed under the GPLv3.

#### [Mhook](http://codefromthe70s.org/mhook24.aspx)

## IRCAM LISTEN HRTF Database

**Applies to:** `assets/hrtf/*.mhr` (HRTF data derived from subject IRC_1037
of the IRCAM LISTEN HRTF Database)

The included HRTF data files are derived from the *Listen HRTF Database*,
recorded in the IRCAM anechoic room (Paris, France) in 2002–2003, principal
investigator Olivier Warusfel, Room Acoustics Team, IRCAM.

The data was obtained from the IRCAM LISTEN database and converted to the
`.mhr` format used by this mod. The original files are distributed in SOFA
and IRCAM native formats at:

- Original project page (archived):
  http://recherche.ircam.fr/equipes/salles/listen/
- SOFA-format mirror (contains `IRC_1037_*` subject files):
  https://sofacoustics.org/data/database/listen (hrtf)
- Catalogue entry (SONICOM Ecosystem, Database #78):
  https://ecosystem.sonicom.eu/databases/78

## X3DAudio HRTF Alma Edition build
- 𝐀𝐈 𝐃𝐈𝐒𝐂𝐋𝐎𝐒𝐔𝐑𝐄: All modifications to the original project was done entirely by Claude AI [https://claude. ai]
- Based on X3DAudio HRTF by kosumosu/ThreeDeeJay (GPLv3): [https://github.com/kosumosu/x3daudio1_7_hrtf & https://github.com/ThreeDeeJay/X3DAudioHRTF]
- Unofficial patch set: see patches/README_PATCHES.txt and CHANGELOG.md
- HRTF data: IRC_1037, see hrtf/CREDITS.md
- Shim (X3DAudio1_5.dll): see shim_source/

Copyright (c) 2007-2012, Marton Anka
Portions Copyright (c) 2007, Matt Conover

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

Copyright (c) 2002 IRCAM (Institut de Recherche et Coordination
Acoustique/Musique). All Rights Reserved.

Use of Materials:

IRCAM hereby grants users permission to use materials available therein
for any purpose — educational, research or commercial. However, each
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

The database is additionally catalogued as **CC BY 4.0 Attribution**
(https://creativecommons.org/licenses/by/4.0/) by the SONICOM Ecosystem
(Database #78).

### Requested acknowledgement

IRCAM requests that use of the database be acknowledged. If you use or
redistribute this project, please preserve this notice file and the credit
above.

**Contact for the original dataset:** hrtf@ircam.fr
**Principal investigator:** Olivier Warusfel, Room Acoustics Team, IRCAM,
1 place Igor Stravinsky, 75004 Paris, France

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
