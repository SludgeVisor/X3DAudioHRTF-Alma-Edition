#include "stdafx.h"
#include "SequencedX3DAudioProxy.h"
#include "proxy/XAudio2VoiceProxy.h"
#include "../common.h"

void SequencedX3DAudioProxy::CorrectVolume(SpatialData & spatialDataCopy, const ChannelMatrix & clientMatrix) const
{
	float volumeSum = 0.0f;
	for (std::size_t destIndex = 0; destIndex < clientMatrix.getDestinationCount(); destIndex++)
	{
		for (std::size_t srcIndex = 0; srcIndex < clientMatrix.getSourceCount(); srcIndex++)
		{
			volumeSum += clientMatrix.getValue(srcIndex, destIndex);
		}
	}
	// This used to be "* 0.5f" to compensate for stereo sources being summed (L + R) in the effect, which also made
	// every mono source 6 dB too quiet. The effect now averages the channels instead, so no compensation is needed.
	spatialDataCopy.volume_multiplier = volumeSum;
}

SpatialData SequencedX3DAudioProxy::ExtractSpatialData(XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy)
{
	SpatialData spatialDataCopy;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		spatialDataCopy = _lastSpatialData;
		_lastSpatialData.present = false;
	}

	const auto clientMatrix = source->getOutputMatrix(pDestinationProxy);
	
	CorrectVolume(spatialDataCopy, clientMatrix);

	return spatialDataCopy;
}

void SequencedX3DAudioProxy::X3DAudioInitialize(UINT32 SpeakerChannelMask, FLOAT32 SpeedOfSound)
{
	_speedOfSound = SpeedOfSound;
}


void SequencedX3DAudioProxy::X3DAudioCalculate(const X3DAUDIO_LISTENER * pListener, const X3DAUDIO_EMITTER * pEmitter, UINT32 Flags, X3DAUDIO_DSP_SETTINGS * pDSPSettings)
{
	const SpatialData result = CommonX3DAudioCalculate(_speedOfSound, pListener, pEmitter, Flags, pDSPSettings);
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_lastSpatialData = result;
	}

	const UINT32 channelCount = pDSPSettings->DstChannelCount * pDSPSettings->SrcChannelCount;
	if (channelCount == 0)
		return;

	const float factor = 1.0f / channelCount;
	for (UINT32 i = 0; i < channelCount; i++)
	{
		pDSPSettings->pMatrixCoefficients[i] = result.volume_multiplier * factor;
	}
}

