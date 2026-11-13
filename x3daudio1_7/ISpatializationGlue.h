#pragma once
#include "graph/ISpatializedDataExtractor.h"
#include "x3daudio-hook/IX3DAudioProxy.h"

class ISpatializationGlue : public ISpatializedDataExtractor, public IX3DAudioProxy
{
	
};
