#pragma once
#include "IHrtfData.h"
#include "HrtfTypes.h"
#include <cstdint>

class IHrtfDataSet
{
public:
	virtual ~IHrtfDataSet() = default;

	virtual bool empty() const = 0;
	virtual bool has_sample_rate(uint32_t sample_rate) const = 0;
	// Sample rate of the stored data closest to the requested one. Only valid if !empty().
	virtual uint32_t find_closest_sample_rate(uint32_t sample_rate) const = 0;
	virtual const IHrtfData & get_sample_rate_data(uint32_t sampl_rate) const = 0;
};
