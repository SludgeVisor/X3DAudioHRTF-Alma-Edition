#include "stdafx.h"
#include "MatrixEncodingX3DAudioProxy.h"
#include "ISound3DRegistry.h"
#include "ChannelMatrixMagic.h"

#include "proxy/XAudio2VoiceProxy.h"
#include "../common.h"


MatrixEncodingX3DAudioProxy::MatrixEncodingX3DAudioProxy(ISound3DRegistry * registry)
	: _registry(registry)
{
}

void MatrixEncodingX3DAudioProxy::X3DAudioInitialize(UINT32 SpeakerChannelMask, FLOAT32 SpeedOfSound)
{
	_speedOfSound = SpeedOfSound;
}

void MatrixEncodingX3DAudioProxy::X3DAudioCalculate(const X3DAUDIO_LISTENER * pListener, const X3DAUDIO_EMITTER * pEmitter, UINT32 Flags, X3DAUDIO_DSP_SETTINGS * pDSPSettings)
{
	const auto spatialData = CommonX3DAudioCalculate(_speedOfSound, pListener, pEmitter, Flags, pDSPSettings);

	Sound3DEntry entry;
	entry.azimuth = spatialData.azimuth;
	entry.distance = spatialData.distance;
	entry.elevation = spatialData.elevation;
	entry.volume_multiplier = spatialData.volume_multiplier;

	const auto id = _registry->CreateEntry(entry);

	embed_sound_id(pDSPSettings->pMatrixCoefficients, pDSPSettings->SrcChannelCount, pDSPSettings->DstChannelCount, id);
}



SpatialData MatrixEncodingX3DAudioProxy::ExtractSpatialData(XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy)
{
	const auto clientMatrix = source->getOutputMatrix(pDestinationProxy);

	if (does_matrix_contain_id(clientMatrix))
	{
		const sound_id id = extract_sound_id(clientMatrix);

		const auto sound3d = _registry->GetEntry(id);

		SpatialData spatialData;
		spatialData.present = true;
		spatialData.volume_multiplier = sound3d.volume_multiplier;
		spatialData.elevation = sound3d.elevation;
		spatialData.azimuth = sound3d.azimuth;
		spatialData.distance = sound3d.distance;

		return spatialData;
	}
	else
	{
		SpatialData spatialData;
		spatialData.present = false;
		return spatialData;
	}
}