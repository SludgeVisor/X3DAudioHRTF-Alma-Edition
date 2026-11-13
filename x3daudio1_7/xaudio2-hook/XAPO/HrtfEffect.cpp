#include "stdafx.h"

#include "HrtfEffect.h"
#include "HrtfConfig.h"
#include "logger.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>

#include <intrin.h>

const UINT32 OUTPUT_CHANNEL_COUNT = 2;

namespace
{
	// Directions closer than this (radians) use the same filter. The data sets have a resolution of several degrees.
	const float DIRECTION_EPSILON = 1.0e-4f;
	const float PI = 3.14159265358979f;

	float wrapped_difference(float a, float b)
	{
		float difference = std::fmod(a - b, 2.0f * PI);
		if (difference > PI)
			difference -= 2.0f * PI;
		else if (difference < -PI)
			difference += 2.0f * PI;
		return difference;
	}

	float finite_or_zero(float value)
	{
		return std::isfinite(value) ? value : 0.0f;
	}

	void log_rate_fallback_once(UINT32 requested, UINT32 used)
	{
		static std::mutex mutex;
		static std::set<UINT32> reported;

		std::lock_guard<std::mutex> lock(mutex);
		if (reported.insert(requested).second)
			logger::logRelease("WARNING [HRTF]: There's no dataset for sample rate ", requested, " Hz, using the ", used, " Hz one instead. Add a ", requested, " Hz .mhr file for exact results.");
	}
}

XAPO_REGISTRATION_PROPERTIES HrtfXapoEffect::m_regProps = {
	__uuidof(HrtfXapoEffect),
	L"HRTF Effect",
	L"Copyright (C)2015 Roman Zavalov",
	1,
	0,
	XAPO_FLAG_FRAMERATE_MUST_MATCH
	| XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH
	| XAPO_FLAG_BUFFERCOUNT_MUST_MATCH
	| XAPO_FLAG_INPLACE_SUPPORTED,
	1, 1, 1, 1 };

HrtfXapoEffect* HrtfXapoEffect::CreateInstance()
{
	// The data files are loaded once; later calls are cheap and do not touch the disk.
	auto hrtf_data = hrtf_config::get_shared_data_set();
	if (!hrtf_data)
		return nullptr;

	try
	{
		auto instance = new HrtfXapoEffect(hrtf_data);
		instance->Initialize(nullptr, 0);
		return instance;
	}
	catch (const std::exception & e)
	{
		logger::logRelease("ERROR [HRTF]: cannot create the HRTF effect: ", e.what());
		return nullptr;
	}
}

HrtfXapoEffect::HrtfXapoEffect(const std::shared_ptr<IHrtfDataSet> & hrtf_data) :
	CXAPOParametersBase(&m_regProps, reinterpret_cast<BYTE*>(m_params), sizeof(HrtfXapoParam), FALSE)
	, m_time_per_frame(0)
	, m_hrtf_data_set(hrtf_data)
	, m_hrtf_data(nullptr)
	, m_invalid_buffers_count(0)
	, m_buffers_per_history(0)
	, m_history_size(0)
	, m_max_frames(0)
	, m_interpolation_mode(1)
	, m_interpolation_taps(2)
	, m_have_current(false)
	, m_crossfade(false)
	, m_gain_valid(false)
	, m_current_gain(0.0f)
{
	m_params[0] = { };
	m_params[1] = { };
	m_params[2] = { };
	m_signal.reserve(192);
	m_scratch_left.impulse_response.reserve(128);
	m_scratch_right.impulse_response.reserve(128);
	m_scratch_left.delay = 0.0f;
	m_scratch_right.delay = 0.0f;
	std::memset(&m_input_format, 0, sizeof(m_input_format));
	std::memset(&m_output_format, 0, sizeof(m_output_format));
}

HRESULT HrtfXapoEffect::LockForProcess(UINT32 InputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS * pInputLockedParameters, UINT32 OutputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS * pOutputLockedParameters)
{
	_ASSERT(pOutputLockedParameters[0].pFormat->nChannels == 2);

	HRESULT hr = CXAPOParametersBase::LockForProcess(
		InputLockedParameterCount,
		pInputLockedParameters,
		OutputLockedParameterCount,
		pOutputLockedParameters);

	if (SUCCEEDED(hr))
	{
		memcpy(&m_input_format, pInputLockedParameters[0].pFormat, sizeof(m_input_format));
		memcpy(&m_output_format, pOutputLockedParameters[0].pFormat, sizeof(m_output_format));

		m_time_per_frame = 1.0f / float(m_input_format.nSamplesPerSec);
		m_hrtf_data = nullptr;

		if (m_hrtf_data_set->empty())
		{
			logger::logRelease("ERROR [HRTF]: There's no HRTF data at all");
		}
		else
		{
			UINT32 used_rate = m_input_format.nSamplesPerSec;

			// Before: a missing sample rate meant the sound was passed through loud and un-spatialized.
			// Now the closest data set is used, which is still positioned and attenuated correctly.
			if (!m_hrtf_data_set->has_sample_rate(used_rate))
			{
				used_rate = m_hrtf_data_set->find_closest_sample_rate(used_rate);
				log_rate_fallback_once(m_input_format.nSamplesPerSec, used_rate);
			}

			m_hrtf_data = &m_hrtf_data_set->get_sample_rate_data(used_rate);
			m_max_frames = std::max<UINT32>(1, pInputLockedParameters[0].MaxFrameCount);
			m_interpolation_mode = std::min(2, std::max(0, hrtf_config::get().delay_interpolation));
			m_interpolation_taps = m_interpolation_mode == 2 ? 8u : (m_interpolation_mode == 1 ? 2u : 1u);

			// The interpolation adds (taps - 1) taps to every filter, which reach that much further into the past.
			m_history_size = m_hrtf_data->get_longest_delay() + m_hrtf_data->get_respoone_length() + (m_interpolation_taps > 2 ? m_interpolation_taps - 2 : 0);

			// number of silent buffers that still have to be rendered to let the filter ring out (rounded up!)
			m_buffers_per_history = (m_history_size + m_max_frames - 1) / m_max_frames;

			m_signal.assign(m_history_size + m_max_frames, 0.0f);
			reset_state();
		}
	}
	return hr;
}

void HrtfXapoEffect::reset_state()
{
	if (!m_signal.empty())
		std::fill(m_signal.begin(), m_signal.begin() + m_history_size, 0.0f);

	m_invalid_buffers_count = m_buffers_per_history; // no tail pending
	m_have_current = false;
	m_crossfade = false;
	m_gain_valid = false;
	m_current_gain = 0.0f;
}

void HrtfXapoEffect::design_fractional_delay(float fraction, float * coefficients) const
{
	// Coefficients g[j] such that  sum_j g[j] * x[n - j]  ~  x(n - delay)
	switch (m_interpolation_mode)
	{
	case 0:
		coefficients[0] = 1.0f; // whole samples only
		break;

	case 1:
		// linear interpolation, delay = fraction
		coefficients[0] = 1.0f - fraction;
		coefficients[1] = fraction;
		break;

	default:
	{
		// 8-tap Blackman windowed sinc for delay = 3 + fraction (centred, so it needs no future samples).
		// The constant 3 sample (68 us at 44.1 kHz) of extra delay is the same for both ears.
		const float delay = 3.0f + fraction;
		float sum = 0.0f;
		for (int j = 0; j < 8; j++)
		{
			const float t = static_cast<float>(j) - delay; // in (-4; 4)
			const float x = PI * t;
			const float sinc = std::fabs(x) < 1.0e-6f ? 1.0f : std::sin(x) / x;
			const float window = 0.42f + 0.5f * std::cos(PI * t / 4.0f) + 0.08f * std::cos(2.0f * PI * t / 4.0f);
			coefficients[j] = sinc * window;
			sum += coefficients[j];
		}
		for (int j = 0; j < 8; j++) // exact gain 1 at DC
			coefficients[j] /= sum;
		break;
	}
	}
}

void HrtfXapoEffect::build_filter(const DirectionData & source, Filter & destination) const
{
	const size_t length = m_hrtf_data->get_respoone_length();
	const int longest_delay = static_cast<int>(m_hrtf_data->get_longest_delay());

	const float delay = std::max(0.0f, finite_or_zero(source.delay));
	int whole = static_cast<int>(delay);
	float fraction = delay - static_cast<float>(whole);
	if (whole > longest_delay)
	{
		whole = longest_delay;
		fraction = 0.0f;
	}

	// y[n] = sum_k h[k] * x(n - whole - fraction - k) and x(t - fraction) ~ sum_j g[j] * x(t - j), therefore the
	// fractional delay can be folded into the filter once per buffer: taps = h convolved with g.
	float g[8];
	design_fractional_delay(fraction, g);

	const size_t interpolation_taps = m_interpolation_taps;
	const auto & h = source.impulse_response;
	destination.taps.assign(length + interpolation_taps - 1, 0.0f);
	for (size_t k = 0; k < length; k++)
	{
		const float value = h[k];
		for (size_t j = 0; j < interpolation_taps; j++)
			destination.taps[k + j] += value * g[j];
	}
	destination.delay = whole;
}

void HrtfXapoEffect::compute_filters(float elevation, float azimuth, float distance, FilterPair & out)
{
	m_hrtf_data->get_direction_data(elevation, azimuth, distance, m_scratch_left, m_scratch_right);
	build_filter(m_scratch_left, out.ear[0]);
	build_filter(m_scratch_right, out.ear[1]);
	out.elevation = elevation;
	out.azimuth = azimuth;
}

void HrtfXapoEffect::select_filters(const HrtfXapoParam & params)
{
	const float elevation = finite_or_zero(params.elevation);
	const float azimuth = finite_or_zero(params.azimuth);
	const float distance = finite_or_zero(params.distance);

	if (!m_have_current)
	{
		compute_filters(elevation, azimuth, distance, m_current);
		m_have_current = true;
		m_crossfade = false;
		return;
	}

	const bool same_direction =
		std::fabs(elevation - m_current.elevation) < DIRECTION_EPSILON &&
		std::fabs(wrapped_difference(azimuth, m_current.azimuth)) < DIRECTION_EPSILON;

	if (same_direction)
	{
		m_crossfade = false;
		return;
	}

	compute_filters(elevation, azimuth, distance, m_next);

	if (hrtf_config::get().crossfade)
	{
		m_crossfade = true; // blended during this buffer, see process_valid_buffer
	}
	else
	{
		std::swap(m_current, m_next); // old behaviour: hard switch at the buffer boundary
		m_crossfade = false;
	}
}

float HrtfXapoEffect::convolve(const UINT32 frame_index, const Filter & filter) const
{
	const int start_signal_index = static_cast<int>(m_history_size + frame_index) - filter.delay;
	const size_t tap_count = filter.taps.size();

	_ASSERT(start_signal_index - static_cast<int>(tap_count) + 1 >= 0);

	const float * signal = m_signal.data();
	const float * taps = filter.taps.data();

	const size_t whole_pack_count = tap_count / 4;
	const size_t remainder_count = tap_count % 4;

	__m128 packed_sum = _mm_setzero_ps();
	for (size_t i = 0; i < whole_pack_count; i++)
	{
		__m128 reversed_signal = _mm_loadu_ps(signal + (start_signal_index - static_cast<int>(i * 4) - 3));
		__m128 signal_values = _mm_shuffle_ps(reversed_signal, reversed_signal, _MM_SHUFFLE(0, 1, 2, 3));
		__m128 response = _mm_loadu_ps(taps + i * 4);
		packed_sum = _mm_add_ps(packed_sum, _mm_mul_ps(signal_values, response));
	}
	packed_sum = _mm_hadd_ps(_mm_hadd_ps(packed_sum, packed_sum), packed_sum);
	float sum = _mm_cvtss_f32(packed_sum);

	for (size_t i = 0; i < remainder_count; i++)
	{
		sum += signal[start_signal_index - static_cast<int>(i + whole_pack_count * 4)] * taps[i + whole_pack_count * 4];
	}
	return sum;
}

void HrtfXapoEffect::shift_history(UINT32 frame_count)
{
	// The history is the last m_history_size samples of the data that was really processed, which ends at
	// m_history_size + frame_count (not at the end of the buffer, which is only reached for full buffers).
	std::memmove(m_signal.data(), m_signal.data() + frame_count, m_history_size * sizeof(float));
}

void HrtfXapoEffect::process_valid_buffer(const float * pInput, float * pOutput, const UINT32 frame_count, const HrtfXapoParam & params)
{
	if (frame_count == 0)
		return;

	const auto & settings = hrtf_config::get();
	const UINT32 channels = std::max<UINT32>(1, m_input_format.nChannels);

	// The signal is stored unscaled; the volume is applied as a ramp after the convolution.
	float * signal = m_signal.data() + m_history_size;
	if (channels == 1)
	{
		std::memcpy(signal, pInput, frame_count * sizeof(float));
	}
	else
	{
		// Average (not sum) of all channels, so a stereo source is as loud as a mono source of the same level.
		const float scale = 1.0f / static_cast<float>(channels);
		for (UINT32 i = 0; i < frame_count; i++)
		{
			float sum = 0.0f;
			for (UINT32 c = 0; c < channels; c++)
				sum += pInput[i * channels + c];
			signal[i] = sum * scale;
		}
	}

	float volume = finite_or_zero(params.volume_multiplier);
	volume = std::min(std::max(volume, 0.0f), 16.0f);
	const float target_gain = volume * settings.hrtf_gain * (channels > 1 ? settings.stereo_source_gain : 1.0f);
	const float start_gain = m_gain_valid ? m_current_gain : target_gain;
	m_gain_valid = true;
	m_current_gain = target_gain;

	const float step = 1.0f / static_cast<float>(frame_count);

	for (UINT32 i = 0; i < frame_count; i++)
	{
		const float t = static_cast<float>(i + 1) * step; // 0 -> 1 across the buffer
		const float gain = start_gain + (target_gain - start_gain) * t;

		float left = convolve(i, m_current.ear[0]);
		float right = convolve(i, m_current.ear[1]);

		if (m_crossfade)
		{
			const float new_left = convolve(i, m_next.ear[0]);
			const float new_right = convolve(i, m_next.ear[1]);
			left += (new_left - left) * t;
			right += (new_right - right) * t;
		}

		pOutput[i * OUTPUT_CHANNEL_COUNT + 0] = left * gain;
		pOutput[i * OUTPUT_CHANNEL_COUNT + 1] = right * gain;
	}

	if (m_crossfade)
	{
		std::swap(m_current, m_next);
		m_crossfade = false;
	}

	shift_history(frame_count);
	m_invalid_buffers_count = 0;
}

void HrtfXapoEffect::process_invalid_buffer(float * pOutput, const UINT32 frames_to_write_count, UINT32 & valid_frames_counter)
{
	const UINT32 frames = std::min(frames_to_write_count, m_max_frames);

	std::fill(m_signal.begin() + m_history_size, m_signal.begin() + m_history_size + frames, 0.0f);

	for (UINT32 i = 0; i < frames; i++)
	{
		pOutput[i * OUTPUT_CHANNEL_COUNT + 0] = convolve(i, m_current.ear[0]) * m_current_gain;
		pOutput[i * OUTPUT_CHANNEL_COUNT + 1] = convolve(i, m_current.ear[1]) * m_current_gain;
	}
	valid_frames_counter = frames;

	shift_history(frames);
	m_invalid_buffers_count++;
}

void HrtfXapoEffect::bypass(const float * pInput, float * pOutput, const UINT32 frame_count, const bool is_input_valid)
{
	if (!is_input_valid)
		return;

	const bool in_place = (pInput == pOutput);
	const UINT32 channels = std::max<UINT32>(1, m_input_format.nChannels);

	if (channels == 1)
	{
		// Going backwards is what makes this safe when input and output are the same buffer.
		for (UINT32 i = frame_count; i-- > 0;)
		{
			const float value = pInput[i];
			pOutput[i * OUTPUT_CHANNEL_COUNT + 0] = value;
			pOutput[i * OUTPUT_CHANNEL_COUNT + 1] = value;
		}
	}
	else if (channels == 2)
	{
		if (!in_place)
		{
			for (UINT32 i = 0; i < frame_count; i++)
			{
				pOutput[i * 2 + 0] = pInput[i * 2 + 0];
				pOutput[i * 2 + 1] = pInput[i * 2 + 1];
			}
		}
	}
	else
	{
		const float scale = 1.0f / static_cast<float>(channels);
		for (UINT32 i = 0; i < frame_count; i++)
		{
			float sum = 0.0f;
			for (UINT32 c = 0; c < channels; c++)
				sum += pInput[i * channels + c];
			pOutput[i * 2 + 0] = sum * scale;
			pOutput[i * 2 + 1] = sum * scale;
		}
	}
}

void HrtfXapoEffect::Process(UINT32 InputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS * pInputProcessParameters, UINT32 OutputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS * pOutputProcessParameters, BOOL IsEnabled)
{
	_ASSERT(IsLocked());
	_ASSERT(InputProcessParameterCount == 1);
	_ASSERT(OutputProcessParameterCount == 1);

	const UINT32 input_frame_count = pInputProcessParameters[0].ValidFrameCount;
	const UINT32 frames_to_write_count = pOutputProcessParameters[0].ValidFrameCount;
	auto pInput = reinterpret_cast<const float*>(pInputProcessParameters[0].pBuffer);
	auto pOutput = reinterpret_cast<float*>(pOutputProcessParameters[0].pBuffer);
	const bool is_input_valid = pInputProcessParameters[0].BufferFlags == XAPO_BUFFER_VALID;

	// copy, the pointer is only valid until EndProcess
	const HrtfXapoParam params = *reinterpret_cast<const HrtfXapoParam *>(BeginProcess());

	if (IsEnabled && m_hrtf_data != nullptr)
	{
		if (is_input_valid)
		{
			const UINT32 frames = std::min(input_frame_count, m_max_frames);

			select_filters(params);

			pOutputProcessParameters[0].BufferFlags = XAPO_BUFFER_VALID;
			pOutputProcessParameters[0].ValidFrameCount = frames;

			process_valid_buffer(pInput, pOutput, frames, params);
		}
		else if (has_history())
		{
			UINT32 valid_frames_counter = 0;
			process_invalid_buffer(pOutput, frames_to_write_count, valid_frames_counter);
			pOutputProcessParameters[0].BufferFlags = XAPO_BUFFER_VALID;
			pOutputProcessParameters[0].ValidFrameCount = valid_frames_counter;
		}
		else
		{
			pOutputProcessParameters[0].BufferFlags = XAPO_BUFFER_SILENT;

			// Forget the old sound completely, so the next one on this voice does not start with its tail.
			if (m_have_current)
				reset_state();
		}
	}
	else
	{
		pOutputProcessParameters[0].BufferFlags = pInputProcessParameters[0].BufferFlags;
		pOutputProcessParameters[0].ValidFrameCount = pInputProcessParameters[0].ValidFrameCount;

		bypass(pInput, pOutput, input_frame_count, is_input_valid);
	}

	EndProcess();
}

bool HrtfXapoEffect::has_history() const
{
	return m_invalid_buffers_count < m_buffers_per_history;
}
