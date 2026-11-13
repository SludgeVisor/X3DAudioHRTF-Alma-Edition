#pragma once
#include <X3DAudio.h>
#include "ISpatializationGlue.h"


class ISound3DRegistry;

class MatrixEncodingX3DAudioProxy final : public ISpatializationGlue
{
public:
	explicit MatrixEncodingX3DAudioProxy(ISound3DRegistry * registry);
	~MatrixEncodingX3DAudioProxy() = default;


	void X3DAudioInitialize(UINT32 SpeakerChannelMask, FLOAT32 SpeedOfSound) override;
	void X3DAudioCalculate(const X3DAUDIO_LISTENER * pListener, const X3DAUDIO_EMITTER * pEmitter, UINT32 Flags, X3DAUDIO_DSP_SETTINGS * pDSPSettings) override;

	SpatialData ExtractSpatialData(XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy) override;

private:
	ISound3DRegistry * _registry;
	float _speedOfSound;


public:
	MatrixEncodingX3DAudioProxy(const MatrixEncodingX3DAudioProxy& other) = delete;
	MatrixEncodingX3DAudioProxy(MatrixEncodingX3DAudioProxy&& other) noexcept = default;
	MatrixEncodingX3DAudioProxy& operator=(const MatrixEncodingX3DAudioProxy& other) = delete;
	MatrixEncodingX3DAudioProxy& operator=(MatrixEncodingX3DAudioProxy&& other) noexcept = default;
};
