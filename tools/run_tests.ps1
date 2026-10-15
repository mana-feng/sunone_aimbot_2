# Run from a Visual Studio x64 developer PowerShell. No capture/input threads start.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    New-Item -ItemType Directory -Path build/tests -Force | Out-Null
    $common = @('/nologo','/EHsc','/std:c++17','/utf-8','/O2','/I','sunone_aimbot_2','/I','sunone_aimbot_2/detector')
    foreach ($test in @('i18n_test','model_settings_test')) {
        & cl.exe @common "tools/tests/$test.cpp" "/Fo:build/tests/$test.obj" "/Fe:build/tests/$test.exe"
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
        & "./build/tests/$test.exe"
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
    }
    & cl.exe @common tools/tests/aim_hotkeys_config_test.cpp sunone_aimbot_2/config/config.cpp /Fo:build/tests/ /Fe:build/tests/aim_hotkeys_config_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed: aim_hotkeys_config_test' }
    & ./build/tests/aim_hotkeys_config_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test failed: aim_hotkeys_config_test' }
    $imgui='sunone_aimbot_2/imgui'
    & cl.exe @common /I $imgui tools/tests/i18n_imgui_test.cpp "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" "$imgui/imgui_widgets.cpp" /Fo:build/tests/ /Fe:build/tests/i18n_imgui_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed: i18n_imgui_test' }
    & ./build/tests/i18n_imgui_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test failed: i18n_imgui_test' }
    $opencv='sunone_aimbot_2/modules/opencv/build/dml'
    $library=Get-ChildItem "$opencv/x64/vc*/lib/opencv_world*.lib" | Where-Object Name -NotMatch 'd\.lib$' | Select-Object -First 1
    if (-not $library) { throw 'Build/install the DirectML OpenCV dependency first.' }
    $oldPath=$env:PATH
    try {
        $env:PATH="$(Split-Path (Split-Path $library.FullName))/bin;$oldPath"
        & cl.exe @common /I "$opencv/include" tools/tests/model_compatibility_test.cpp sunone_aimbot_2/detector/postProcess.cpp $library.FullName /Fo:build/tests/ /Fe:build/tests/model_compatibility_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed: model_compatibility_test' }
        & ./build/tests/model_compatibility_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test failed: model_compatibility_test' }
        & cl.exe @common /I sunone_aimbot_2/config /I "$opencv/include" tools/tests/target_roles_test.cpp sunone_aimbot_2/mouse/AimbotTarget.cpp $library.FullName /Fo:build/tests/ /Fe:build/tests/target_roles_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed: target_roles_test' }
        & ./build/tests/target_roles_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test failed: target_roles_test' }
    } finally { $env:PATH=$oldPath }
} finally { Pop-Location }
