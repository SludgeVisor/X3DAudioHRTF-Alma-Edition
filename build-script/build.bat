:tools_paths
@set "VSINSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
@set "SEVENZ_DIR=C:\Program Files\7-Zip"

@set "PATH=%VSINSTALLDIR%\MSBuild\15.0\bin;%PATH%"
@set "PATH=%SEVENZ_DIR%;%PATH%"


:set_variables
@set "VERSION=2.4"
@set "PRODUCT_NAME=xaudio_hrtf"


:set_subpaths
@set "INTERIM_DIR_NAME=interim"
@set "INTERIM_DIR=..\%INTERIM_DIR_NAME%"
@set "COMPILATION_DIR=%INTERIM_DIR%\compilation"
@set "UNCOMPRESSED_PACKAGE_DIR=%INTERIM_DIR%\package"
@set "FINAL_PACKAGE_DIR=..\final_package"
@set "HRTF_DATASET_DIR=..\hrtf\KEMAR"

call build_platform Win32
call build_platform x64

echo Build Complete