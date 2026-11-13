#include "stdafx.h"
#include "application.h"

#include "ISpatializationGlue.h"

#include <memory>
#include "x3daudio-hook/Matrix/MatrixEncodingX3DAudioProxy.h"
#include "x3daudio-hook/Matrix/Sound3DRegistry.h"
#include "x3daudio-hook/Sequenced/SequencedX3DAudioProxy.h"

#define GLUE_SEQUENCED

std::unique_ptr<ISpatializationGlue> s_glue;


void EnsureInitialized()
{
	if (s_glue == nullptr)
	{
#if defined(GLUE_MATRIX_ENCODING)
		s_glue = std::make_unique<MatrixEncodingX3DAudioProxy>(&Sound3DRegistry::GetInstance());
#elif defined(GLUE_SEQUENCED)
		s_glue = std::make_unique<SequencedX3DAudioProxy>();
#endif
	}
}

ISpatializedDataExtractor & getSpatializedDataExtractor()
{
	EnsureInitialized();
	return *s_glue;
}

IX3DAudioProxy & getX3DAudioProxy()
{
	EnsureInitialized();
	return *s_glue;
}
