#include "stdafx.h"

#include "AudioGraphMapper.h"

#include "proxy/XAudio2VoiceProxy.h"
#include "proxy/XAudio2MasteringVoiceProxy.h"
#include "proxy/XAudio2SourceVoiceProxy.h"
#include "proxy/XAudio2SubmixVoiceProxy.h"

#include "ISpatializedDataExtractor.h"

#include "wave/WaveFile.h"

#include "util.h"
#include "common_types.h"
#include "logger.h"
#include <vector>
#include <mutex>
#include <stdexcept>
#include <xaudio2-hook/XAPO/HrtfEffect.h>

XAUDIO2_VOICE_SENDS AudioGraphMapper::_emptySends = empty_sends();


AudioGraphMapper::AudioGraphMapper(IXAudio2 * xaudio, ISpatializedDataExtractor * spatializedDataExtractor)
	: m_xaudio(xaudio)
	, _spatializedDataExtractor(spatializedDataExtractor)
	, m_masteringNode(nullptr) 
{}

AudioGraphMapper::~AudioGraphMapper()
{
}



IXAudio2SourceVoice* AudioGraphMapper::CreateSourceVoice(const WAVEFORMATEX* pSourceFormat, UINT32 Flags, float MaxFrequencyRatio, IXAudio2VoiceCallback* pCallback, const XAUDIO2_VOICE_SENDS* pSendList, const XAUDIO2_EFFECT_CHAIN* pEffectChain)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	IXAudio2SourceVoice * actualVoiceRawPtr = nullptr;
	const HRESULT createResult = m_xaudio->CreateSourceVoice(&actualVoiceRawPtr, pSourceFormat, Flags, MaxFrequencyRatio, pCallback, &_emptySends, pEffectChain);
	if (FAILED(createResult) || actualVoiceRawPtr == nullptr)
	{
		logger::logRelease("ERROR: CreateSourceVoice failed with HRESULT ", static_cast<long>(createResult));
		return nullptr;
	}
	std::shared_ptr<IXAudio2SourceVoice> actualVoice(actualVoiceRawPtr, [](IXAudio2SourceVoice * voice) { voice->DestroyVoice(); });

	auto proxyVoice = new XAudio2SourceVoiceProxy(pSourceFormat->nChannels, pSourceFormat->nSamplesPerSec, Flags, MaxFrequencyRatio, pCallback, -1, from_XAUDIO2_VOICE_SENDS(pSendList), from_XAUDIO2_EFFECT_CHAIN(pEffectChain));

	{
		std::unique_ptr<Node> node = std::make_unique<Node>();
		node->inputSampleRate = pSourceFormat->nSamplesPerSec;
		node->inputChannelsCount = pSourceFormat->nChannels;
		node->actualProcessingStage = -1;
		// (was [EffectCount], which is one past the last descriptor)
		node->mainOutputChannelsCount = (pEffectChain && pEffectChain->EffectCount > 0) ? pEffectChain->pEffectDescriptors[pEffectChain->EffectCount - 1].OutputChannels : node->inputChannelsCount;
		node->proxyVoice.reset(proxyVoice);
		node->mainVoice = actualVoice;
		m_nodes.insert(std::make_pair(proxyVoice, std::move(node)));
	}

	proxyVoice->stateGetter = [actualVoice](XAudio2SourceVoiceProxy* source, XAUDIO2_VOICE_STATE& out_state)
	{
		actualVoice->GetState(&out_state);
	};

	proxyVoice->onStart = [actualVoice](XAudio2VoiceProxy * source, UINT32 flags, UINT32 operationSet)
	{
		actualVoice->Start(flags, operationSet);
	};

	proxyVoice->onStop = [actualVoice](XAudio2VoiceProxy * source, UINT32 flags, UINT32 operationSet)
	{
		actualVoice->Stop(flags, operationSet);
	};

	WAVEFORMATEX sourceFormat = *pSourceFormat;
	proxyVoice->onSubmitSourceBuffer = [actualVoice, sourceFormat](XAudio2SourceVoiceProxy * source, const XAUDIO2_BUFFER * pBuffer, const XAUDIO2_BUFFER_WMA * pBufferWma)
	{
		{
			//WaveFile waveFile(std::wstring(L"wavs\\") + std::to_wstring(sourceFormat.nChannels) + L"_" + get_name(static_cast<XAudio2VoiceProxy*>(source)) + L".wav", sourceFormat.nChannels, sourceFormat.nSamplesPerSec, sourceFormat.wBitsPerSample);
			//waveFile.AppendData(pBuffer->pAudioData, pBuffer->AudioBytes);
		}

		actualVoice->SubmitSourceBuffer(pBuffer, pBufferWma);
	};

	proxyVoice->onFlushSourceBuffers = [actualVoice](XAudio2SourceVoiceProxy * source)
	{
		actualVoice->FlushSourceBuffers();
	};

	proxyVoice->onDiscontinuity = [actualVoice](XAudio2SourceVoiceProxy * source)
	{
		actualVoice->Discontinuity();
	};

	proxyVoice->onExitLoop = [actualVoice](XAudio2SourceVoiceProxy * source, UINT32 operationSet)
	{
		actualVoice->ExitLoop(operationSet);
	};

	proxyVoice->onSetFrequencyRatio = [actualVoice](XAudio2SourceVoiceProxy * source, UINT32 operationSet)
	{
		actualVoice->SetFrequencyRatio(source->getFrequencyRatio(), operationSet);
	};

	proxyVoice->onSetSourceSampleRate = [actualVoice](XAudio2SourceVoiceProxy * source)
	{
		actualVoice->SetSourceSampleRate(source->getInputSampleRate());
	};

	setupCommonCallbacks(proxyVoice, actualVoice);

	resetSendsForVoice(proxyVoice);

	return proxyVoice;
}

IXAudio2SubmixVoice* AudioGraphMapper::CreateSubmixVoice(UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 ProcessingStage, const XAUDIO2_VOICE_SENDS* pSendList, const XAUDIO2_EFFECT_CHAIN* pEffectChain)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	const UINT32 actualProcessingStage = std::min(ProcessingStage, UINT_MAX / 2 - 1) * 2 + 1; // we make room for intermediate 'tail' voices. May fails in some cases.

	IXAudio2SubmixVoice * actualVoiceRawPtr = nullptr;
	const HRESULT createResult = m_xaudio->CreateSubmixVoice(&actualVoiceRawPtr, InputChannels, InputSampleRate, Flags, actualProcessingStage, &_emptySends, pEffectChain);
	if (FAILED(createResult) || actualVoiceRawPtr == nullptr)
	{
		logger::logRelease("ERROR: CreateSubmixVoice failed with HRESULT ", static_cast<long>(createResult));
		return nullptr;
	}
	std::shared_ptr<IXAudio2SubmixVoice> actualVoice(actualVoiceRawPtr, [](IXAudio2SubmixVoice * voice) { voice->DestroyVoice(); });

	auto proxyVoice = new XAudio2SubmixVoiceProxy(InputChannels, InputSampleRate, Flags, ProcessingStage, from_XAUDIO2_VOICE_SENDS(pSendList), from_XAUDIO2_EFFECT_CHAIN(pEffectChain));

	{
		std::unique_ptr<Node> node = std::make_unique<Node>();
		node->inputSampleRate = InputSampleRate;
		node->inputChannelsCount = InputChannels;
		node->actualProcessingStage = actualProcessingStage;
		node->mainOutputChannelsCount = (pEffectChain && pEffectChain->EffectCount > 0) ? pEffectChain->pEffectDescriptors[pEffectChain->EffectCount - 1].OutputChannels : node->inputChannelsCount;
		node->proxyVoice.reset(proxyVoice);
		node->mainVoice = actualVoice;
		m_nodes.insert(std::make_pair(proxyVoice, std::move(node)));
	}

	setupCommonCallbacks(proxyVoice, actualVoice);

	resetSendsForVoice(proxyVoice);

	return proxyVoice;
}

IXAudio2MasteringVoice* AudioGraphMapper::CreateMasteringVoice(UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 DeviceIndex, const XAUDIO2_EFFECT_CHAIN* pEffectChain)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	// For HRTF only two-channel mastering voice makes sense.
	IXAudio2MasteringVoice * actualVoiceRawPtr = nullptr;
	const HRESULT createResult = m_xaudio->CreateMasteringVoice(&actualVoiceRawPtr, 2, InputSampleRate, Flags, DeviceIndex, pEffectChain);
	if (FAILED(createResult) || actualVoiceRawPtr == nullptr)
	{
		logger::logRelease("ERROR: CreateMasteringVoice failed with HRESULT ", static_cast<long>(createResult));
		return nullptr;
	}
	std::shared_ptr<IXAudio2MasteringVoice> actualVoice(actualVoiceRawPtr, [](IXAudio2MasteringVoice * voice) { voice->DestroyVoice(); });

	auto proxyVoice = new XAudio2MasteringVoiceProxy(InputChannels, InputSampleRate, Flags, DeviceIndex, from_XAUDIO2_EFFECT_CHAIN(pEffectChain));

	{
		std::unique_ptr<Node> node = std::make_unique<Node>();
		node->inputSampleRate = InputSampleRate;
		node->inputChannelsCount = 2;
		node->actualProcessingStage = std::numeric_limits<UINT32>::max();
		node->mainOutputChannelsCount = (pEffectChain && pEffectChain->EffectCount > 0) ? pEffectChain->pEffectDescriptors[pEffectChain->EffectCount - 1].OutputChannels : node->inputChannelsCount;
		node->proxyVoice.reset(proxyVoice);
		node->mainVoice = actualVoice;
		m_nodes.insert(std::make_pair(proxyVoice, std::move(node)));
	}

	setupCommonCallbacks(proxyVoice, actualVoice);

	m_masteringNode = m_nodes[proxyVoice].get();
	return proxyVoice;
}

VoiceSends AudioGraphMapper::mapProxySendsToActualOnes(const VoiceSends & proxySends) const
{
	voice_sends actualSends(proxySends.getVoices().size());
	std::transform(std::begin(proxySends.getVoices()), std::end(proxySends.getVoices()), std::begin(actualSends),
		[&](const XAUDIO2_SEND_DESCRIPTOR & sendDesc)
	{
		XAUDIO2_SEND_DESCRIPTOR actualSendDesc;
		actualSendDesc.Flags = sendDesc.Flags;
		actualSendDesc.pOutputVoice = getNodeForProxyVoice(sendDesc.pOutputVoice)->mainVoice.get();
		return actualSendDesc;
	});

	return VoiceSends(actualSends);
}



void AudioGraphMapper::setupCommonCallbacks(XAudio2VoiceProxy* proxyVoice, const std::shared_ptr<IXAudio2Voice> & actualVoice)
{
	proxyVoice->onSetOutputVoices = [this](XAudio2VoiceProxy * source, matrices_map & outputMatrices, filter_params_map & outputFilterParams)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		resetSendsForVoice(source);

		// XAudio2 resets the matrices and filters of the new outputs to their defaults (this used to be a stub
		// that stored a nonsensical 1x1 matrix with the value 666).
		const Node * sourceNode = getNodeForProxyVoice(source->asXAudio2Voice());

		outputMatrices.clear();
		outputFilterParams.clear();
		for (auto & sendDesc : source->getSends().getVoices())
		{
			const Node * destinationNode = getNodeForProxyVoice(sendDesc.pOutputVoice);
			outputMatrices.insert(std::make_pair(sendDesc.pOutputVoice, makeDefaultChannelMatrix(sourceNode->mainOutputChannelsCount, destinationNode->inputChannelsCount)));

			XAUDIO2_FILTER_PARAMETERS params;
			params.Type = LowPassFilter;
			params.Frequency = 1.0f;
			params.OneOverQ = 1.0f;
			outputFilterParams.insert(std::make_pair(sendDesc.pOutputVoice, params));
		}
	};

	proxyVoice->onSetEffectChain = [actualVoice](XAudio2VoiceProxy * source)
	{
		auto chain = source->getEffectChain();
		if (chain.size() > 0)
		{
			XAUDIO2_EFFECT_CHAIN chainStruct;
			chainStruct.EffectCount = static_cast<UINT32>(chain.size());
			chainStruct.pEffectDescriptors = &*chain.begin();
			actualVoice->SetEffectChain(&chainStruct);
		}
		else
		{
			actualVoice->SetEffectChain(nullptr);
		}
	};

	proxyVoice->onSetEffectEnabled = [actualVoice](XAudio2VoiceProxy * source, UINT32 effectIndex, UINT32 operationSet)
	{
		if (source->getIsEffectEnabled(effectIndex))
			actualVoice->EnableEffect(effectIndex, operationSet);
		else
			actualVoice->DisableEffect(effectIndex, operationSet);
	};

	proxyVoice->onSetEffectParameters = [actualVoice](XAudio2VoiceProxy * source, UINT32 effectIndex, UINT32 operationSet)
	{
		auto params = source->getEffectParameters(effectIndex);
		actualVoice->SetEffectParameters(effectIndex, &*params.begin(), static_cast<UINT32>(params.size()), operationSet);
	};

	proxyVoice->onSetFilterParameters = [actualVoice](XAudio2VoiceProxy * source, UINT32 operationSet)
	{
		auto params = source->getFilterParameters();
		actualVoice->SetFilterParameters(&params, operationSet);
	};

	proxyVoice->onSetOutputFilterParameters = [this, actualVoice](XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy, UINT32 operationSet)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		Node* node = getNodeForProxyVoice(source->asXAudio2Voice());
		Node* destinationNode = getNodeForProxyVoice(pDestinationProxy);
		auto tail = node->tailVoices.find(destinationNode);
		if (tail == node->tailVoices.end())
			return;

		if (!tail->second.isSpatialized) // There's no need to add filters, i guess.
		{
			auto params = source->getOutputFilterParameters(pDestinationProxy);
			actualVoice->SetOutputFilterParameters(tail->second.voice.get(), &params, operationSet);
		}
	};

	proxyVoice->onSetVolume = [actualVoice](XAudio2VoiceProxy * source, UINT32 operationSet)
	{
		actualVoice->SetVolume(source->getVolume(), operationSet);
	};

	proxyVoice->onSetChannelVolumes = [actualVoice](XAudio2VoiceProxy * source, UINT32 operationSet)
	{
		auto volumes = source->getChannelVolumes();
		actualVoice->SetChannelVolumes(static_cast<UINT32>(volumes.size()), &*volumes.begin(), operationSet);
	};

	proxyVoice->onSetOutputMatrix = [this](XAudio2VoiceProxy * source, IXAudio2Voice * pDestinationProxy, UINT32 operationSet)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		Node * const node = getNodeForProxyVoice(source->asXAudio2Voice());

		Node * destinationNode = nullptr;
		if (pDestinationProxy != nullptr)
		{
			destinationNode = getNodeForProxyVoice(pDestinationProxy);
		}
		else if (node->tailVoices.size() == 1)
		{
			destinationNode = node->tailVoices.begin()->first;
		}
		else
		{
			// Used to throw, which ends in the game. XAudio2 itself just fails the call.
			logger::logRelease("WARNING: onSetOutputMatrix ", source, " Cannot set output matrix to voice 'nullptr' when the number of output voices is not 1.");
			return;
		}

		auto tailIterator = node->tailVoices.find(destinationNode);
		if (tailIterator == node->tailVoices.end())
		{
			logger::logRelease("WARNING: onSetOutputMatrix ", source, " The destination voice is not one of the outputs of the source voice.");
			return;
		}
		TailVoiceDescriptor & tailVoiceDescriptor = tailIterator->second;

		const auto spatializedData = _spatializedDataExtractor->ExtractSpatialData(source, pDestinationProxy);

		bool spatialize = spatializedData.present;

		if (spatialize)
		{
			if (node->mainOutputChannelsCount > 2)
				logger::logRelease(L"WARNING: onSetOutputMatrix ", source, " Trying to spatialize voice with more than two channels");

			// Builds the HRTF tail voice the first time. If that is impossible (no HRTF data), the sound simply
			// stays a normally panned, correctly attenuated sound.
			if (!tailVoiceDescriptor.isSpatialized)
				spatialize = swapTailVoice(node, destinationNode, tailVoiceDescriptor, true);
		}

		if (spatialize)
		{
			HrtfXapoParam params;
			params.volume_multiplier = spatializedData.volume_multiplier;
			params.elevation = spatializedData.elevation;
			params.azimuth = spatializedData.azimuth;
			params.distance = spatializedData.distance;

			tailVoiceDescriptor.voice->SetEffectParameters(0, &params, sizeof(params), XAUDIO2_COMMIT_NOW);
			if (!tailVoiceDescriptor.effectEnabled)
			{
				tailVoiceDescriptor.voice->EnableEffect(0, XAUDIO2_COMMIT_NOW);
				tailVoiceDescriptor.effectEnabled = true;
			}
		}
		else
		{
			if (tailVoiceDescriptor.isSpatialized)
				swapTailVoice(node, destinationNode, tailVoiceDescriptor, false);

			const auto clientMatrix = source->getOutputMatrix(pDestinationProxy);

			ChannelMatrix matrix;

			if (destinationNode->inputChannelsCount == clientMatrix.getDestinationCount())
			{
				matrix = std::move(clientMatrix);
			}
			else if (destinationNode->inputChannelsCount == 2)
			{
				matrix = adaptChannelMatrixToStereoOutput(clientMatrix);
			}
			else
			{
				logger::logRelease("WARNING: onSetOutputMatrix ", source, " Sender output channels count does not match sendee input channels count and sendee input channels count is not 2. Matrix ignored.");
				return;
			}

			std::vector<float> values;
			values.resize(matrix.getSourceCount() * matrix.getDestinationCount());
			from_ChannelMatrix(matrix, matrix.getSourceCount(), matrix.getDestinationCount(), &*values.begin());

			tailVoiceDescriptor.voice->SetOutputMatrix(destinationNode->mainVoice.get(), matrix.getSourceCount(), matrix.getDestinationCount(), &*values.begin());
		}
	};

	proxyVoice->onDestroyVoice = [this](XAudio2VoiceProxy * source)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		auto nodeIterator = m_nodes.find(source->asXAudio2Voice());
		if (nodeIterator == m_nodes.end())
			return;

		Node * const node = nodeIterator->second.get();

		// Voices that still send to the destroyed voice lose that route (before: they kept a pointer to freed memory).
		for (Node * predecessor : m_edges.getPredecessors(node))
		{
			auto tail = predecessor->tailVoices.find(node);
			if (tail == predecessor->tailVoices.end())
				continue;

			std::shared_ptr<IXAudio2SubmixVoice> tailToDestroy = tail->second.voice;
			predecessor->tailVoices.erase(tail);
			updateSendsForMainVoice(predecessor); // nothing sends to the tail any more when it is destroyed below
		}

		// Before, the edges of the destroyed voice stayed in the repository forever.
		m_edges.removeVertex(node);

		if (m_masteringNode == node)
			m_masteringNode = nullptr;

		// Must stay the last statement: this destroys the proxy voice this callback belongs to.
		m_nodes.erase(nodeIterator);
	};
}

const Node * AudioGraphMapper::getNodeForProxyVoice(IXAudio2Voice* pDestination) const
{
	return m_nodes.at(pDestination).get();
}

Node * AudioGraphMapper::getNodeForProxyVoice(IXAudio2Voice* pDestination)
{
	return m_nodes.at(pDestination).get();
}



void AudioGraphMapper::resetSendsForVoice(XAudio2VoiceProxy* proxyVoice)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	Node* node = getNodeForProxyVoice(proxyVoice->asXAudio2Voice());

	m_edges.removeAllSuccessors(node);

	node->mainVoice->SetOutputVoices(&_emptySends);
	node->tailVoices.clear();

	std::vector<XAUDIO2_SEND_DESCRIPTOR> proxySends;

	if (!proxyVoice->getSends().getSendToMasterOnly())
	{
		proxySends = proxyVoice->getSends().getVoices();
	}
	else if (m_masteringNode != nullptr)
	{
		XAUDIO2_SEND_DESCRIPTOR masterSendDescriptor;
		masterSendDescriptor.Flags = 0;
		masterSendDescriptor.pOutputVoice = m_masteringNode->proxyVoice.get();
		proxySends.push_back(masterSendDescriptor);
	}

	for (auto & proxySend : proxySends)
	{
		Node* sendNode = getNodeForProxyVoice(proxySend.pOutputVoice);
		auto tailVoice = createTailVoice(node, sendNode, effect_chain());
		if (!tailVoice)
			throw std::runtime_error("Cannot create an intermediate voice");

		TailVoiceDescriptor tailVoiceDesc;
		tailVoiceDesc.voice = std::move(tailVoice);
		tailVoiceDesc.flags = proxySend.Flags;
		tailVoiceDesc.isSpatialized = false;
		node->tailVoices.insert(std::make_pair(sendNode, std::move(tailVoiceDesc)));

		m_edges.addEdge(node, sendNode);
	}

	updateSendsForMainVoice(node);
}

void AudioGraphMapper::updateSendsForMainVoice(Node* node)
{
	std::vector<XAUDIO2_SEND_DESCRIPTOR> sendsToTailsVector;

	for (auto & tail : node->tailVoices)
	{
		XAUDIO2_SEND_DESCRIPTOR tailSendDescriptor;
		tailSendDescriptor.Flags = tail.second.flags;
		tailSendDescriptor.pOutputVoice = tail.second.voice.get();

		sendsToTailsVector.push_back(tailSendDescriptor);
	}

	auto sendsToTails = VoiceSends(sendsToTailsVector);
	XAUDIO2_VOICE_SENDS actualSendsStruct = from_voice_sends(sendsToTails);
	node->mainVoice->SetOutputVoices(&actualSendsStruct);
}

std::shared_ptr<IXAudio2SubmixVoice> AudioGraphMapper::createTailVoice(Node * senderNode, Node* sendNode, const effect_chain & effectChain)
{
	XAUDIO2_SEND_DESCRIPTOR sendDescriptor;
	sendDescriptor.pOutputVoice = sendNode->mainVoice.get();
	sendDescriptor.Flags = 0;
	XAUDIO2_VOICE_SENDS sends;
	sends.SendCount = 1;
	sends.pSends = &sendDescriptor;

	IXAudio2SubmixVoice * tailVoice = nullptr;
	HRESULT result;
	if (effectChain.size() == 0)
	{
		result = m_xaudio->CreateSubmixVoice(&tailVoice, senderNode->mainOutputChannelsCount, senderNode->inputSampleRate, 0, senderNode->actualProcessingStage + 1, &sends, nullptr);
	}
	else
	{
		XAUDIO2_EFFECT_CHAIN chainStruct;
		chainStruct.EffectCount = static_cast<UINT32>(effectChain.size());
		chainStruct.pEffectDescriptors = const_cast<XAUDIO2_EFFECT_DESCRIPTOR*>(&*effectChain.begin());
		result = m_xaudio->CreateSubmixVoice(&tailVoice, senderNode->mainOutputChannelsCount, senderNode->inputSampleRate, 0, senderNode->actualProcessingStage + 1, &sends, &chainStruct);
	}

	if (FAILED(result) || tailVoice == nullptr)
	{
		logger::logRelease("ERROR: cannot create an intermediate submix voice, HRESULT ", static_cast<long>(result));
		return nullptr;
	}

	return std::shared_ptr<IXAudio2SubmixVoice>(tailVoice, [](IXAudio2Voice * pVoice) { pVoice->DestroyVoice(); });
}

bool AudioGraphMapper::swapTailVoice(Node * senderNode, Node * sendNode, TailVoiceDescriptor & descriptor, bool withHrtf)
{
	effect_chain chain;
	IUnknown * effect = nullptr;
	ULONG ownedReferences = 0;

	if (withHrtf)
	{
		HrtfXapoEffect * hrtfEffect = HrtfXapoEffect::CreateInstance();
		if (hrtfEffect == nullptr)
			return false;

		effect = static_cast<IUnknown *>(static_cast<IXAPOParameters *>(hrtfEffect));
		ownedReferences = effect->AddRef(); // reference count after this call == number of references we hold

		XAUDIO2_EFFECT_DESCRIPTOR effectDesc;
		effectDesc.pEffect = effect;
		effectDesc.InitialState = false;
		effectDesc.OutputChannels = 2;
		chain.push_back(effectDesc);
	}

	// The new voice is created while the sender still plays through the old one.
	std::shared_ptr<IXAudio2SubmixVoice> newTail = createTailVoice(senderNode, sendNode, chain);

	// XAudio2 added its own reference when it accepted the effect chain. Ours was never released before, so the
	// effect and its buffers were never freed after the voice was destroyed. Whether CXAPOBase starts counting at 0 or
	// at 1 is not visible here, so the number of references to drop comes from the AddRef above. (If the voice could
	// not be created, this frees the effect.)
	if (effect != nullptr && ownedReferences >= 1 && ownedReferences <= 2)
	{
		for (ULONG i = 0; i < ownedReferences; i++)
			effect->Release();
	}

	if (!newTail)
		return false;

	std::swap(descriptor.voice, newTail);
	descriptor.isSpatialized = withHrtf;
	descriptor.effectEnabled = false;

	// One call replaces the output list of the sender: it never has "no output" in between, which used to be
	// a window where the sound was dropped. Only afterwards the old tail (now in newTail) is destroyed.
	updateSendsForMainVoice(senderNode);
	newTail.reset();

	return true;
}
