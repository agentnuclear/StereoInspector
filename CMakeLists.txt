cmake_minimum_required(VERSION 3.20)
project(StereoInspector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs video features2d highgui)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Tesseract QUIET)

set(SOURCES
    src/main.cpp
    src/core/Frame.h
    src/core/Frame.cpp
    src/core/Types.h
    src/config/Config.h
    src/config/Config.cpp
    src/capture/DxgiCapture.h
    src/capture/DxgiCapture.cpp
    src/analysis/IStereoAnalyzer.h
    src/analysis/IStereoAnalyzer.cpp
    src/analysis/Analyzer.h
    src/analysis/Analyzer.cpp
    src/analysis/FeatureCache.h
    src/analysis/FeatureCache.cpp
    src/analysis/IssueClassifier.h
    src/analysis/IssueClassifier.cpp
    src/analysis/RegionMerger.h
    src/analysis/RegionMerger.cpp
    src/analysis/DataCollector.h
    src/analysis/DataCollector.cpp
    src/analysis/modules/SsimAnalyzer.h
    src/analysis/modules/SsimAnalyzer.cpp
    src/analysis/modules/PixelDiffAnalyzer.h
    src/analysis/modules/PixelDiffAnalyzer.cpp
    src/analysis/modules/HistogramAnalyzer.h
    src/analysis/modules/HistogramAnalyzer.cpp
    src/analysis/modules/EdgeAnalyzer.h
    src/analysis/modules/EdgeAnalyzer.cpp
    src/analysis/modules/OrbAnalyzer.h
    src/analysis/modules/OrbAnalyzer.cpp
    src/analysis/modules/OpticalFlowAnalyzer.h
    src/analysis/modules/OpticalFlowAnalyzer.cpp
    src/analysis/modules/BlurAnalyzer.h
    src/analysis/modules/BlurAnalyzer.cpp
    src/analysis/modules/BrightnessAnalyzer.h
    src/analysis/modules/BrightnessAnalyzer.cpp
    src/analysis/modules/ContrastAnalyzer.h
    src/analysis/modules/ContrastAnalyzer.cpp
    src/analysis/modules/BloomAnalyzer.h
    src/analysis/modules/BloomAnalyzer.cpp
    src/analysis/modules/ShadowAnalyzer.h
    src/analysis/modules/ShadowAnalyzer.cpp
    src/analysis/modules/StereoOffsetAnalyzer.h
    src/analysis/modules/StereoOffsetAnalyzer.cpp
    src/analysis/modules/OcrAnalyzer.h
    src/analysis/modules/OcrAnalyzer.cpp
    src/analysis/modules/StereoDetector.h
    src/analysis/modules/StereoDetector.cpp
    src/analysis/modules/VrLensAnalyzer.h
    src/analysis/modules/VrLensAnalyzer.cpp
    src/analysis/modules/StereoCorrespondence.h
    src/analysis/modules/StereoCorrespondence.cpp
    src/analysis/modules/TemporalAnalyzer.h
    src/analysis/modules/TemporalAnalyzer.cpp
    src/overlay/Overlay.h
    src/overlay/Overlay.cpp
    src/visualization/Visualizer.h
    src/visualization/Visualizer.cpp
    src/logging/Logger.h
    src/logging/Logger.cpp
    src/logging/ReportGenerator.h
    src/logging/ReportGenerator.cpp
    src/input/InputManager.h
    src/input/InputManager.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui.h")
    list(APPEND SOURCES
        deps/imgui/imgui.cpp
        deps/imgui/imgui_draw.cpp
        deps/imgui/imgui_widgets.cpp
        deps/imgui/imgui_tables.cpp
        deps/imgui/backends/imgui_impl_win32.cpp
        deps/imgui/backends/imgui_impl_dx11.cpp
    )
    list(APPEND IMGUI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
    list(APPEND IMGUI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/backends")
endif()

add_executable(StereoInspector WIN32 ${SOURCES})

target_include_directories(StereoInspector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${spdlog_INCLUDE_DIRS}
    ${nlohmann_json_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIRS}
)

if(Tesseract_FOUND)
    target_compile_definitions(StereoInspector PRIVATE HAS_TESSERACT=1)
    target_include_directories(StereoInspector PRIVATE ${Tesseract_INCLUDE_DIRS})
    target_link_libraries(StereoInspector PRIVATE ${Tesseract_LIBRARIES})
    if(WIN32 AND MSVC)
        # Copy Tesseract DLLs to output directory
        foreach(_tess_lib ${Tesseract_LIBRARIES})
            get_filename_component(_tess_lib_dir "${_tess_lib}" DIRECTORY)
            file(GLOB _tess_dlls "${_tess_lib_dir}/*.dll"
                                "${_tess_lib_dir}/../bin/*.dll")
            foreach(_tess_dll ${_tess_dlls})
                add_custom_command(TARGET StereoInspector POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_tess_dll}"
                        "$<TARGET_FILE_DIR:StereoInspector>/"
                )
            endforeach()
        endforeach()
    endif()
endif()

target_link_libraries(StereoInspector PRIVATE
    ${OpenCV_LIBS}
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    dxguid
    d2d1
    dwrite
    gdi32
    user32
    shell32
    comctl32
    Imm32
    version
    Winmm
    comdlg32
    ole32
    oleaut32
    Shlwapi
    UxTheme
)

target_compile_options(StereoInspector PRIVATE /W4 /utf-8 /MP /Oi /fp:fast)
target_compile_definitions(StereoInspector PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
