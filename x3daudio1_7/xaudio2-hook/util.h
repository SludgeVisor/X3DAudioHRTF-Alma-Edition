#pragma once

#include "ChannelMatrix.h"

#include <XAudio2.h>
#include <sstream>
#include <vector>
#include <cstdint>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <string>

#include "common_types.h"
#include "HrtfConfig.h"
#include <cmath>

inline void print_sends(std::wstringstream & ss, const XAUDIO2_VOICE_SENDS * pSendList)
{
	ss << "sends [";
	if (pSendList)
	{
		if (pSendList->SendCount > 1)
		{
			for (UINT32 i = 0; i < pSendList->SendCount - 1; i++)
			{
				ss << pSendList->pSends[i].pOutputVoice << ", ";
			}
		}
		if (pSendList->SendCount > 0)
			ss << pSendList->pSends[pSendList->SendCount - 1].pOutputVoice;
	}
	ss << "]";
}

inline VoiceSends from_XAUDIO2_VOICE_SENDS(const XAUDIO2_VOICE_SENDS * pSendList)
{
	if (pSendList)
	{
		voice_sends sends;
		sends.resize(pSendList->SendCount);
		std::copy(pSendList->pSends, pSendList->pSends + pSendList->SendCount, std::begin(sends));
		return VoiceSends(sends);
	}
	else
	{
		return VoiceSends();
	}
}

inline effect_chain from_XAUDIO2_EFFECT_CHAIN(const XAUDIO2_EFFECT_CHAIN * pEffectChain)
{
	effect_chain effects;
	if (pEffectChain)
	{
		effects.resize(pEffectChain->EffectCount);
		std::copy(pEffectChain->pEffectDescriptors, pEffectChain->pEffectDescriptors + pEffectChain->EffectCount, std::begin(effects));
	}
	return effects;
}

template <typename T>
inline std::vector<T> buffer_to_vector(const T * pBuffer, UINT32 elementCount)
{
	std::vector<T> buf;
	buf.resize(elementCount);
	std::copy(pBuffer, pBuffer + elementCount, std::begin(buf));
	return buf;
}

inline std::vector<int8_t> buffer_to_vector(const void * pBuffer, UINT32 elementCount)
{
	return buffer_to_vector(reinterpret_cast<const int8_t *>(pBuffer), elementCount);
}


// TODO : const T * pBuffer should not compile!!!!
template<typename T>
inline void vector_to_buffer(const std::vector<T> & vec, T * pBuffer, UINT32 elementCount)
{
	if (vec.size() != elementCount)
		throw std::logic_error("Buffer size mismatch");
	
	std::copy(std::begin(vec), std::end(vec), pBuffer);
}

inline void vector_to_buffer(const std::vector<int8_t> & vec, void * pBuffer, UINT32 bufferByteSize)
{
	vector_to_buffer(vec, reinterpret_cast<int8_t*>(pBuffer), bufferByteSize);
}

inline void from_ChannelMatrix(const ChannelMatrix & matrix, UINT32 sourceChannels, UINT32 destinationChannels, float * pLevelMatrix)
{
	if (matrix.getSourceCount() != sourceChannels || matrix.getDestinationCount() != destinationChannels)
		throw std::invalid_argument("Matrix size mismatch");

	for (UINT32 destIndex = 0; destIndex < destinationChannels; destIndex++)
	{
		for (UINT32 srcIndex = 0; srcIndex < sourceChannels; srcIndex++)
		{
			pLevelMatrix[destIndex * sourceChannels + srcIndex] = matrix.getValue(srcIndex, destIndex);
		}
	}
}

inline std::wstring get_name(const void * ptr)
{
	std::wstringstream ss;
	auto result = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	ss << std::put_time(std::localtime(&result), L"%H-%M-%S") << L"_";
	ss << ptr;
	return ss.str();
}

inline XAUDIO2_VOICE_SENDS from_voice_sends(VoiceSends & sends)
{
	if (sends.getSendToMasterOnly())
		throw std::logic_error("Cannot map sends to mastering voice.");

	XAUDIO2_VOICE_SENDS sendsStruct;
	sendsStruct.SendCount = static_cast<UINT32>(sends.getVoices().size());
	sendsStruct.pSends = sends.getVoices().empty() ? nullptr : const_cast<XAUDIO2_SEND_DESCRIPTOR*>(&*sends.getVoices().begin());
	return sendsStruct;
}

inline XAUDIO2_VOICE_SENDS empty_sends()
{
	XAUDIO2_VOICE_SENDS emptySends;
	emptySends.SendCount = 0;
	emptySends.pSends = nullptr;
	return emptySends;
}

// Default output matrix of a voice that was never given one: identity, and a mono source goes to the front pair.
inline ChannelMatrix makeDefaultChannelMatrix(unsigned sourceChannels, unsigned destinationChannels)
{
	ChannelMatrix matrix(sourceChannels, destinationChannels);

	if (sourceChannels == 1 && destinationChannels >= 2)
	{
		matrix.setValue(0, 0, 1.0f);
		matrix.setValue(0, 1, 1.0f);
	}
	else
	{
		const unsigned count = sourceChannels < destinationChannels ? sourceChannels : destinationChannels;
		for (unsigned i = 0; i < count; i++)
			matrix.setValue(i, i, 1.0f);
	}

	return matrix;
}

// Folds a matrix that targets a speaker layout with several channels down to the stereo mastering voice.
// Used for everything that is NOT spatialized by the HRTF effect (music, UI, 2D sounds, reverb returns ...).
//
// Speaker layouts according to MSDN (X3DAudio / XAudio2 default channel masks):
//   3: FL FR LFE                       4: FL FR BL BR                  5: FL FR FC SL SR
//   6: FL FR FC LFE SL SR              7: FL FR FC LFE SL SR BC        8: FL FR FC LFE BL BR SL SR
//
// The front speakers now keep their full level. The previous table scaled them by 0.5 (-6 dB), which is what made
// everything that bypasses the HRTF effect noticeably quieter than the game on a real 5.1 setup. To stay safe against
// clipping, a source channel that would end up louder than full scale on one side is scaled back to full scale.
inline ChannelMatrix adaptChannelMatrixToStereoOutput(const ChannelMatrix & sourceMatrix)
{
	const unsigned sourceCount = sourceMatrix.getSourceCount();
	const unsigned destinationCount = sourceMatrix.getDestinationCount();

	if (destinationCount == 2)
		return sourceMatrix;

	const float centre = 0.70710678f; // -3 dB into both sides
	const float surround = 0.70710678f;
	const float lfe = 0.5f;

	struct Weights { float left; float right; };
	std::vector<Weights> weights(destinationCount, Weights{ 0.0f, 0.0f });

	switch (destinationCount)
	{
	case 1:
		weights[0] = { 1.0f, 1.0f };
		break;
	case 3:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { lfe, lfe };
		break;
	case 4:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { surround, 0.0f }; weights[3] = { 0.0f, surround };
		break;
	case 5:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { centre, centre };
		weights[3] = { surround, 0.0f }; weights[4] = { 0.0f, surround };
		break;
	case 6:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { centre, centre }; weights[3] = { lfe, lfe };
		weights[4] = { surround, 0.0f }; weights[5] = { 0.0f, surround };
		break;
	case 7:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { centre, centre }; weights[3] = { lfe, lfe };
		weights[4] = { surround, 0.0f }; weights[5] = { 0.0f, surround }; weights[6] = { surround, surround };
		break;
	case 8:
		weights[0] = { 1.0f, 0.0f }; weights[1] = { 0.0f, 1.0f }; weights[2] = { centre, centre }; weights[3] = { lfe, lfe };
		weights[4] = { 0.5f, 0.0f }; weights[5] = { 0.0f, 0.5f }; weights[6] = { 0.5f, 0.0f }; weights[7] = { 0.0f, 0.5f };
		break;
	default:
		// More than 8 channels: no implicit positions, use the first two as front left / right (this used to throw).
		if (destinationCount >= 2)
		{
			weights[0] = { 1.0f, 0.0f };
			weights[1] = { 0.0f, 1.0f };
		}
		break;
	}

	const float extra_gain = hrtf_config::get().non_hrtf_gain;

	ChannelMatrix matrix(sourceCount, 2);
	for (unsigned i = 0; i < sourceCount; i++)
	{
		float left = 0.0f;
		float right = 0.0f;
		for (unsigned d = 0; d < destinationCount; d++)
		{
			const float value = sourceMatrix.getValue(i, d);
			left += value * weights[d].left;
			right += value * weights[d].right;
		}

		const float peak = std::max(left, right);
		if (peak > 1.0f)
		{
			left /= peak;
			right /= peak;
		}

		matrix.setValue(i, 0, left * extra_gain);
		matrix.setValue(i, 1, right * extra_gain);
	}
	return matrix;
}
