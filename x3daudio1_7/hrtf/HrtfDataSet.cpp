#include "stdafx.h"
#include "HrtfDataSet.h"
#include "logger.h"
#include <fstream>
#include <algorithm>
#include <cstdlib>

HrtfDataSet::HrtfDataSet(const std::vector<std::wstring> & data_files)
{
	for (auto& data_file_name : data_files)
	{
		try
		{
			std::ifstream file(data_file_name, std::fstream::binary);

			if (!file.is_open())
				throw std::logic_error("Cannot open file");

			HrtfData data(file);
			const uint32_t rate = data.get_sample_rate();

			// Two files with the same sample rate: keep the first one, as before.
			if (m_data.find(rate) != m_data.end())
				logger::logRelease("WARNING [HRTF]: ", data_file_name, " has the same sample rate (", rate, " Hz) as a file loaded earlier and is ignored");
			else
				m_data.insert({ rate, std::move(data) });
		}
		catch (const std::exception & e)
		{
			logger::logRelease("ERROR [HRTF]: skipping ", data_file_name, ": ", e.what());
		}
	}
}

bool HrtfDataSet::empty() const
{
	return m_data.empty();
}

bool HrtfDataSet::has_sample_rate(uint32_t sample_rate) const
{
	return m_data.find(sample_rate) != m_data.end();
}

uint32_t HrtfDataSet::find_closest_sample_rate(uint32_t sample_rate) const
{
	_ASSERT(!m_data.empty());

	uint32_t best = m_data.begin()->first;
	uint32_t best_difference = UINT32_MAX;
	for (const auto & entry : m_data)
	{
		const uint32_t difference = entry.first > sample_rate ? entry.first - sample_rate : sample_rate - entry.first;
		if (difference < best_difference)
		{
			best = entry.first;
			best_difference = difference;
		}
	}
	return best;
}

const IHrtfData & HrtfDataSet::get_sample_rate_data(uint32_t sampl_rate) const
{
	return m_data.at(sampl_rate);
}
