X3DAudio HRTF core - patch set for F.E.A.R. 2 (based on x3daudio1_7_hrtf 2.4.1.0.28, GPLv3)
===============================================================================================

HOW TO BUILD
  * Locally: open x3daudio1_7.sln in Visual Studio 2017/2019/2022 with the DirectX SDK (June 2010) installed
    (DXSDK_DIR must be set), configuration Release | x86. The project files still say v141 / SDK 10.0.15063;
    change the toolset / SDK to whatever you have installed if needed.
  * GitHub: push this tree, the workflow in .github/workflows/build.yml builds Win32 Release and uploads
    x3daudio1_7.dll as an artifact. (Written without being able to run it - see "Not verified" below.)

HOW TO USE WITH THE FEAR2 SHIM PACKAGE
  Rename the built x3daudio1_7.dll to x3daudio_hrtf_core.dll and replace the one in the game folder. Keep the
  hrtf folder where it is. Optional settings: copy x3daudio_hrtf.ini.sample, rename it to x3daudio_hrtf_core.ini.

WHAT CHANGED (file -> reason)
  1. Non-HRTF sounds were quiet          xaudio2-hook/util.h
       The 5.1 -> stereo fold-down scaled FL/FR by 0.5 (-6 dB). Now FL/FR keep full level, centre/surround use
       -3 dB, and a source channel is scaled back if it would exceed full scale on one side. No more exception
       for more than 8 channels. NonHrtfGain in the ini adjusts it.
  2. Fixed -6 dB on HRTF sounds           x3daudio-hook/Sequenced/SequencedX3DAudioProxy.cpp, XAPO/HrtfEffect.cpp
       The "* 0.5" existed because stereo sources were summed (L+R). Channels are now averaged, and the 0.5 is
       gone. Mono sources are +6 dB; stereo sources are unchanged. HrtfGain / StereoSourceGain in the ini.
  3. Sound tails were cut off             XAPO/HrtfEffect.cpp
       The history length was an integer division that was always 0. Now rounded up.
  4. Delay rounded to whole samples       XAPO/HrtfEffect.cpp
       The fractional part of the interaural delay is folded into the filter taps once per buffer. Three modes
       (ini: DelayInterpolation): 0 whole samples (old), 1 linear, 2 = 8-tap windowed sinc (default).
  5. asin without clamp                   x3daudio-hook/common.h, hrtf/HrtfData.cpp
       asin argument clamped; elevation / azimuth are sanitized (NaN, infinity, out of range) before indexing.
       Broken .mhr files (truncated, empty tables) are rejected with an error instead of crashing later.
  6. hrtf folder by relative path         HrtfConfig.cpp/.h
       Looked up next to the dll first (then the old relative path), loaded once, failures remembered (no disk
       scan for every new sound). A bad file is skipped instead of failing everything. Missing sample rate
       falls back to the closest available data set instead of playing loud and un-spatialized.
  7. Graph rebuild on first positional update  xaudio2-hook/graph/AudioGraphMapper.cpp
       The new HRTF voice is created first, the sender is re-pointed with ONE SetOutputVoices call, and only then
       the old voice is destroyed. Before, the sender briefly had no output.
  8. Stale audio history                  XAPO/HrtfEffect.cpp
       History is taken from the end of the real data (not of the buffer) and cleared when a voice goes silent.
  9. Possible memory leak                 AudioGraphMapper.cpp (swapTailVoice)
       The creator's reference to the effect is released after XAudio2 took its own.
 10. Dangling graph entries / threads     graph/EdgeRepository.h, AudioGraphMapper.cpp, SequencedX3DAudioProxy.*
       Destroying a voice now removes its edges and detaches voices that sent to it. Graph and shared spatial
       data are protected by mutexes. Failed voice creation returns an error instead of crashing.
 11. Dormant cases                        AudioGraphMapper.cpp, HrtfEffect.cpp, x3daudio_hook.cpp, dllmain.cpp
       Off-by-one in the source-voice effect chain, the "666" placeholder matrix, >2 channel inputs, in-place
       mono bypass, the system x3daudio1_7.dll no longer being mandatory (it was never used), exceptions in DllMain.
 12. Clicks on fast turns                 XAPO/HrtfEffect.cpp
       Filter and volume are cross-faded sample by sample inside each buffer (only while the direction changes).
       The result is continuous across buffer boundaries. Crossfade=0 in the ini restores the old behaviour.

NOT VERIFIED (read this)
  * This was NOT compiled with MSVC and NOT run in the game. The audio code (HrtfEffect, HrtfData, fold-down) was
    compiled with clang against stand-in headers and tested on Linux against the original code; the graph mapper
    was only syntax-checked against stand-in XAudio2 headers. x3daudio-hook/common.h and
    SequencedX3DAudioProxy.cpp could not be compiled at all here (the math library needs MSVC); their edits are
    small and were checked by reading only.
  * HrtfConfig.cpp (Windows API) and the GitHub workflow have never been run.
  * Whether CXAPOBase starts its reference count at 0 or 1 is not visible in the source; swapTailVoice handles
    both, but a wrong guess would be a crash on voice destruction, so test with logging on.
  * Level changes (items 1 and 2) can clip loud sounds (the laser). If it does, lower HrtfGain / NonHrtfGain.
