#pragma once


class XAudio2VoiceProxy;
struct IXAudio2Voice;

struct SpatialData
{
	// Defines if there's any spatial data at all.
	bool present = false;
	// The following members are only valid if present == true;
	float volume_multiplier = 0.0f;
	float elevation = 0.0f;
	float azimuth = 0.0f;
	float distance = 0.0f;
};

class ISpatializedDataExtractor
{
public:
	virtual ~ISpatializedDataExtractor() = default;

	virtual SpatialData ExtractSpatialData(XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy) = 0;
};
