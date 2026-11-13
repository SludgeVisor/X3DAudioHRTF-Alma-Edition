#pragma once

#include "hrtf/HrtfDataSet.h"

#define NOMINMAX
#include <xapobase.h>

#include "math/math_types.h"

#include <vector>
#include <memory>


struct HrtfXapoParam
{
	// sound volume. Distance attenuation and the game's own volume, already combined. Not limited to [0; 1].
	float volume_multiplier;

	// elevation, in listener's local space, in radians in the range [-pi/2; +pi/2] where 0 is horizontal direction.
	float elevation;

	// azimuth, in listener's local space, in radians in the range [-2pi; +2pi] where 0 is forward direction.
	float azimuth;

	// distance to the source [0; +inf)
	float distance;
};


class __declspec(uuid("{80D4BED4-7605-4E4C-B29C-5579C375C599}")) HrtfXapoEffect : public CXAPOParametersBase
{
public:
	// Returns nullptr if no HRTF data could be loaded (the caller then falls back to plain stereo panning).
	// The returned object carries the reference count CXAPOBase starts with; see AudioGraphMapper::swapTailVoice.
	static HrtfXapoEffect* CreateInstance();

public:
	HrtfXapoEffect(const std::shared_ptr<IHrtfDataSet> & hrtf_data);

	// Inherited via CXAPOParametersBase
	STDMETHOD(LockForProcess)(UINT32 InputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS * pInputLockedParameters, UINT32 OutputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS * pOutputLockedParameters) override;
	STDMETHOD_(void, Process)(UINT32 InputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS * pInputProcessParameters, UINT32 OutputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS * pOutputProcessParameters, BOOL IsEnabled) override;

private:
	// One ear's FIR filter. The fractional part of the interaural delay is folded into the taps
	// (interpolation, see HrtfConfig.h: DelayInterpolation), the whole samples are kept in 'delay'.
	// taps.size() == response length + interpolation taps - 1.
	struct Filter
	{
		std::vector<float> taps;
		int delay = 0;
	};

	struct FilterPair
	{
		Filter ear[2]; // left, right
		float elevation = 0.0f;
		float azimuth = 0.0f;
	};

	bool has_history() const;
	void reset_state();
	void compute_filters(float elevation, float azimuth, float distance, FilterPair & out);
	void build_filter(const DirectionData & source, Filter & destination) const;
	void design_fractional_delay(float fraction, float * coefficients) const;
	void select_filters(const HrtfXapoParam & params);
	void process_valid_buffer(const float * pInput, float * pOutput, const UINT32 frame_count, const HrtfXapoParam & params);
	void process_invalid_buffer(float * pOutput, const UINT32 frames_to_write_count, UINT32 & valid_frames_counter);
	void shift_history(UINT32 frame_count);
	void bypass(const float * pInput, float * pOutput, const UINT32 frame_count, const bool is_valid);
	float convolve(const UINT32 frame_index, const Filter & filter) const;
	static XAPO_REGISTRATION_PROPERTIES m_regProps;

	WAVEFORMATEX m_input_format;
	WAVEFORMATEX m_output_format;
	HrtfXapoParam m_params[3]; // ring buffer as CXAPOParametersBase requires
	float m_time_per_frame;

	std::shared_ptr<IHrtfDataSet> m_hrtf_data_set;
	const IHrtfData * m_hrtf_data;
	std::vector<float> m_signal; // [history | current buffer], unscaled mono mix of the input
	UINT32 m_invalid_buffers_count;
	UINT32 m_buffers_per_history;
	UINT32 m_history_size;
	UINT32 m_max_frames;
	int m_interpolation_mode;      // 0 whole samples, 1 linear, 2 windowed sinc
	UINT32 m_interpolation_taps;   // 1, 2 or 8

	// The filters used for the previous buffer (m_current) and, while the direction changes, the ones
	// to cross-fade to (m_next).
	FilterPair m_current;
	FilterPair m_next;
	bool m_have_current;
	bool m_crossfade;
	bool m_gain_valid;
	float m_current_gain;

	DirectionData m_scratch_left;
	DirectionData m_scratch_right;
};
