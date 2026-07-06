param(
    [string]$VcpkgDir = "$env:USERPROFILE\vcpkg",
    [switch]$SkipVcpkg,
    [switch]$InstallTesseract
)

Write-Host "=== Stereo Inspector Setup ===" -ForegroundColor Cyan
Write-Host ""

if (-not $SkipVcpkg) {
    # Check for Visual Studio 2022
    $vsPath = & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" 2>$null
    if (-not $?) {
        Write-Host "Warning: Visual Studio 2022 not detected at default path." -ForegroundColor Yellow
        Write-Host "Make sure you have Visual Studio 2022 with C++ tools installed." -ForegroundColor Yellow
    }

    if (-not (Test-Path $VcpkgDir)) {
        Write-Host "Installing vcpkg..." -ForegroundColor Yellow
        git clone https://github.com/Microsoft/vcpkg.git $VcpkgDir
        Push-Location $VcpkgDir
        cmd.exe /c "bootstrap-vcpkg.bat -disableMetrics"
        Pop-Location
    } else {
        Write-Host "vcpkg found at $VcpkgDir" -ForegroundColor Green
    }

    Write-Host "Installing OpenCV + spdlog + nlohmann-json..." -ForegroundColor Yellow
    & "$VcpkgDir\vcpkg" install opencv4[highgui] spdlog nlohmann-json --triplet x64-windows

    if ($InstallTesseract) {
        Write-Host "Installing Tesseract..." -ForegroundColor Yellow
        & "$VcpkgDir\vcpkg" install tesseract --triplet x64-windows
    }
}

$depsDir = "deps"
if (-not (Test-Path "$depsDir\imgui\imgui.h")) {
    Write-Host "Downloading Dear ImGui..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path "$depsDir\imgui\backends" | Out-Null

    $imguiVersion = "v1.91.0"
    $imguiUrl = "https://github.com/ocornut/imgui/archive/refs/tags/$imguiVersion.zip"
    $zipFile = "$env:TEMP\imgui.zip"

    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $imguiUrl -OutFile $zipFile -UseBasicParsing

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zipFile, "$env:TEMP\imgui_extract")

        $extracted = Get-ChildItem "$env:TEMP\imgui_extract" -Directory | Select-Object -First 1
        if ($extracted) {
            Copy-Item "$($extracted.FullName)\imgui.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imgui.cpp" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imgui_draw.cpp" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imgui_widgets.cpp" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imgui_tables.cpp" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imconfig.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imgui_internal.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imstb_rectpack.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imstb_textedit.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\imstb_truetype.h" "$depsDir\imgui\"
            Copy-Item "$($extracted.FullName)\backends\imgui_impl_win32.cpp" "$depsDir\imgui\backends\"
            Copy-Item "$($extracted.FullName)\backends\imgui_impl_win32.h" "$depsDir\imgui\backends\"
            Copy-Item "$($extracted.FullName)\backends\imgui_impl_dx11.cpp" "$depsDir\imgui\backends\"
            Copy-Item "$($extracted.FullName)\backends\imgui_impl_dx11.h" "$depsDir\imgui\backends\"
            Write-Host "Dear ImGui $imguiVersion downloaded and extracted" -ForegroundColor Green
        }
        Remove-Item "$env:TEMP\imgui_extract" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item $zipFile -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Host "Failed to download Dear ImGui: $_" -ForegroundColor Red
        Write-Host "Please manually download from https://github.com/ocornut/imgui and extract to deps/imgui/" -ForegroundColor Yellow
    }
} else {
    Write-Host "Dear ImGui found" -ForegroundColor Green
}

if (-not (Test-Path "config.json")) {
    Write-Host "Creating default config.json..." -ForegroundColor Yellow
    @'
{
    "stereoRegion": {
        "autoSplit": true,
        "x": 0,
        "y": 0,
        "width": 0,
        "height": 0
    },
    "thresholds": {
        "ssimWarning": 0.85,
        "ssimFail": 0.70,
        "pixelDiffWarning": 5.0,
        "pixelDiffFail": 15.0,
        "histogramWarning": 0.80,
        "histogramFail": 0.60,
        "edgeWarning": 0.80,
        "edgeFail": 0.60,
        "blurDeltaWarning": 3.0,
        "blurDeltaFail": 8.0,
        "brightnessDeltaWarning": 0.10,
        "brightnessDeltaFail": 0.25,
        "contrastDeltaWarning": 0.15,
        "contrastDeltaFail": 0.30,
        "chromaticAberrationWarning": 0.10,
        "chromaticAberrationFail": 0.25,
        "bloomWarning": 0.10,
        "bloomFail": 0.25,
        "shadowWarning": 0.10,
        "shadowFail": 0.25,
        "stereoOffsetWarning": 10.0,
        "stereoOffsetFail": 30.0,
        "minFeatureMatches": 20,
        "opticalFlowWarning": 5.0,
        "opticalFlowFail": 15.0,
        "ocrMismatchWarning": 2,
        "ocrMismatchFail": 5
    },
    "logging": {
        "csvPath": "stereo_inspector_log.csv",
        "jsonPath": "stereo_inspector_log.json",
        "screenshotDir": "screenshots",
        "reportPath": "stereo_inspector_report.html",
        "autoScreenshotOnFail": true,
        "autoScreenshotOnWarning": false,
        "logTimestamps": true,
        "maxScreenshots": 1000
    },
    "targetFps": 90,
    "captureAdapter": 0,
    "captureOutput": 0,
    "enableOcr": false,
    "startMinimized": false,
    "language": "en"
}
'@ | Out-File -Encoding utf8 -FilePath "config.json"
    Write-Host "config.json created" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "To build:" -ForegroundColor White
Write-Host "  mkdir build && cd build" -ForegroundColor Gray
Write-Host "  cmake .. -DCMAKE_TOOLCHAIN_FILE=$VcpkgDir\scripts\buildsystems\vcpkg.cmake" -ForegroundColor Gray
Write-Host "  cmake --build . --config Release" -ForegroundColor Gray
