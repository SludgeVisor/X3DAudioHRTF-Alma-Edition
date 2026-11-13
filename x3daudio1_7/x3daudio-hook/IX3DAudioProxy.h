#pragma once
#include <X3DAudio.h>

class IX3DAudioProxy
{
public:
	virtual ~IX3DAudioProxy() = default;
	virtual void X3DAudioInitialize(UINT32 SpeakerChannelMask, FLOAT32 SpeedOfSound) = 0;
	virtual void X3DAudioCalculate(const X3DAUDIO_LISTENER * pListener, const X3DAUDIO_EMITTER * pEmitter, UINT32 Flags, X3DAUDIO_DSP_SETTINGS * pDSPSettings) = 0;
};

