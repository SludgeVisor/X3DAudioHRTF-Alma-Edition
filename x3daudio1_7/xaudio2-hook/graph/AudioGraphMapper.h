#pragma once

#include "EdgeRepository.h"

#include <xaudio2.h>
#include <map>
#include <memory>
#include <mutex>
#include <xaudio2-hook/common_types.h>


class ISpatializedDataExtractor;
class XAudio2SourceVoiceProxy;
class XAudio2VoiceProxy;

class TailVoiceDescriptor
{
public:
	std::shared_ptr<IXAudio2SubmixVoice> voice;
	UINT32 flags = 0;
	bool isSpatialized = false;
	bool effectEnabled = false; // the HRTF effect is created disabled and enabled once, with its first parameters
};

class Node
{
public:
	Node() = default;
	Node(const Node& other) = delete;
	Node& operator=(const Node& other) = delete;

	~Node()
	{
		proxyVoice.reset();
		mainVoice.reset();
		tailVoices.clear(); // must be destroyed last
	}

	std::shared_ptr<IXAudio2Voice> proxyVoice;
	std::shared_ptr<IXAudio2Voice> mainVoice;
	// maps proxy send to actual tail voice
	std::map<Node*, TailVoiceDescriptor> tailVoices;

	UINT32 actualProcessingStage = 0;
	UINT32 inputChannelsCount = 0;
	UINT32 inputSampleRate = 0;
	UINT32 mainOutputChannelsCount = 0;
};

class AudioGraphMapper
{
public:
	// AudioGraphMapper does not own xaudio
	AudioGraphMapper(IXAudio2 * xaudio, ISpatializedDataExtractor * spatializedDataExtractor);
	AudioGraphMapper(const AudioGraphMapper &) = delete;
	AudioGraphMapper& operator=(const AudioGraphMapper& other) = delete;

	~AudioGraphMapper();
	IXAudio2SourceVoice * CreateSourceVoice(
		const WAVEFORMATEX * pSourceFormat,
		UINT32 Flags,
		float MaxFrequencyRatio,
		IXAudio2VoiceCallback * pCallback,
		const XAUDIO2_VOICE_SENDS * pSendList,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);

	IXAudio2SubmixVoice * CreateSubmixVoice(
		UINT32 InputChannels,
		UINT32 InputSampleRate,
		UINT32 Flags,
		UINT32 ProcessingStage,
		const XAUDIO2_VOICE_SENDS * pSendList,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);

	IXAudio2MasteringVoice * CreateMasteringVoice(
		UINT32 InputChannels,
		UINT32 InputSampleRate,
		UINT32 Flags,
		UINT32 DeviceIndex,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);


private:
	// The game may create, route and destroy voices from more than one thread. Recursive, because the callbacks
	// below call each other.
	mutable std::recursive_mutex m_mutex;

	IXAudio2 * m_xaudio;
	ISpatializedDataExtractor * _spatializedDataExtractor;
	std::map<IXAudio2Voice*, std::unique_ptr<Node>> m_nodes;
	Node * m_masteringNode;
	EdgeRepository<Node*> m_edges;

	static XAUDIO2_VOICE_SENDS _emptySends;

	VoiceSends mapProxySendsToActualOnes(const VoiceSends & proxySends) const;
	const Node * getNodeForProxyVoice(IXAudio2Voice* pDestination) const;
	Node * getNodeForProxyVoice(IXAudio2Voice* pDestination);
	void setupCommonCallbacks(XAudio2VoiceProxy* proxyVoice, const std::shared_ptr<IXAudio2Voice> & actualVoice);
	void resetSendsForVoice(XAudio2VoiceProxy* proxyVoice);
	void updateSendsForMainVoice(Node * node);
	// Returns nullptr if XAudio2 refuses to create the voice.
	std::shared_ptr<IXAudio2SubmixVoice> createTailVoice(Node * senderNode, Node * sendNode, const effect_chain & effectChain);
	// Replaces the tail voice of a send by one with or without the HRTF effect, without ever leaving the sender
	// without an output. Returns false (and changes nothing) if that was not possible.
	bool swapTailVoice(Node * senderNode, Node * sendNode, TailVoiceDescriptor & descriptor, bool withHrtf);
};