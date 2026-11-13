#include "stdafx.h"
#include "HrtfConfig.h"

#include "hrtf/HrtfDataSet.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <vector>

namespace
{
	std::wstring module_file_path()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&module_file_path), &module))
		{
			return std::wstring();
		}

		wchar_t buffer[MAX_PATH * 2];
		const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH * 2);
		if (length == 0 || length >= MAX_PATH * 2)
			return std::wstring();

		return std::wstring(buffer, length);
	}

	float read_float(const std::wstring & ini_path, const wchar_t * key, float default_value, float min_value, float max_value)
	{
		wchar_t buffer[64];
		GetPrivateProfileStringW(L"Audio", key, L"", buffer, 64, ini_path.c_str());
		if (buffer[0] == 0)
			return default_value;

		wchar_t * end = nullptr;
		const double value = std::wcstod(buffer, &end);
		if (end == buffer || !std::isfinite(value))
			return default_value;

		return static_cast<float>(std::min<double>(max_value, std::max<double>(min_value, value)));
	}

	hrtf_config::Settings load_settings()
	{
		hrtf_config::Settings settings;

		std::wstring path = module_file_path();
		if (path.size() < 4)
			return settings;

		const std::wstring ini_path = path.substr(0, path.size() - 4) + L".ini"; // "foo.dll" -> "foo.ini"
		if (GetFileAttributesW(ini_path.c_str()) == INVALID_FILE_ATTRIBUTES)
			return settings;

		settings.hrtf_gain = read_float(ini_path, L"HrtfGain", settings.hrtf_gain, 0.0f, 8.0f);
		settings.stereo_source_gain = read_float(ini_path, L"StereoSourceGain", settings.stereo_source_gain, 0.0f, 8.0f);
		settings.non_hrtf_gain = read_float(ini_path, L"NonHrtfGain", settings.non_hrtf_gain, 0.0f, 8.0f);
		settings.crossfade = read_float(ini_path, L"Crossfade", settings.crossfade ? 1.0f : 0.0f, 0.0f, 1.0f) >= 0.5f;
		settings.delay_interpolation = static_cast<int>(read_float(ini_path, L"DelayInterpolation", static_cast<float>(settings.delay_interpolation), 0.0f, 2.0f) + 0.5f);

		logger::logRelease("Settings from ", ini_path, ": HrtfGain=", settings.hrtf_gain, " StereoSourceGain=", settings.stereo_source_gain,
			" NonHrtfGain=", settings.non_hrtf_gain, " Crossfade=", settings.crossfade ? 1 : 0, " DelayInterpolation=", settings.delay_interpolation);
		return settings;
	}

	std::vector<std::wstring> find_data_files(const std::wstring & directory)
	{
		std::vector<std::wstring> files;

		WIN32_FIND_DATAW find_data;
		HANDLE find = FindFirstFileW((directory + L"*.mhr").c_str(), &find_data);
		if (find == INVALID_HANDLE_VALUE)
			return files;

		do
		{
			if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				files.push_back(directory + find_data.cFileName);
		} while (FindNextFileW(find, &find_data));

		FindClose(find);
		return files;
	}
}

namespace hrtf_config
{
	const Settings & get()
	{
		static const Settings settings = load_settings();
		return settings;
	}

	std::wstring module_directory()
	{
		const std::wstring path = module_file_path();
		const auto slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
	}

	std::shared_ptr<IHrtfDataSet> get_shared_data_set()
	{
		static std::mutex mutex;
		static std::shared_ptr<IHrtfDataSet> data_set;
		static bool attempted = false;

		std::lock_guard<std::mutex> lock(mutex);
		if (attempted)
			return data_set;
		attempted = true;

		try
		{
			const std::wstring candidates[] = { module_directory() + L"hrtf\\", std::wstring(L"hrtf\\") };

			for (const auto & directory : candidates)
			{
				const auto files = find_data_files(directory);
				if (files.empty())
					continue;

				auto loaded = std::make_shared<HrtfDataSet>(files);
				if (loaded->empty())
					continue;

				logger::logRelease("Loaded HRTF data from ", directory);
				data_set = loaded;
				return data_set;
			}

			logger::logRelease("ERROR [HRTF]: no usable .mhr files found in '", module_directory(), "hrtf'. Sounds will not be spatialized.");
		}
		catch (const std::exception & e)
		{
			logger::logRelease("ERROR [HRTF]: cannot load HRTF data: ", e.what());
		}

		return data_set;
	}
}
