param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe",
    [string]$MainProject = (Join-Path (Split-Path $PSScriptRoot -Parent) "..\RailInspectionCar_Inno_V1"),
    [switch]$RenderPreview
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $env:TEMP "remote_control_tests"
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $root
try {
    & python Tests/generate_oled_font.py --check
    if ($LASTEXITCODE -ne 0) { throw "中文字库或排版资源未同步" }
    $binary = Join-Path $build "test_arm3.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP Tests/test_arm3.c APP/remote_arm3.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "ARM3 remote compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "ARM3 remote tests failed" }
    $binary = Join-Path $build "test_oled_i2c.exe"
    # The untouched drawing library uses mixed signed coordinates; its font
    # tables intentionally use flat initializers for multidimensional arrays.
    & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-sign-compare -Wno-missing-braces -ITests/oled_mocks -IBSP/OLED `
        Tests/test_oled_i2c.c BSP/OLED/OLED.c BSP/OLED/OLED_Data.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "OLED硬件I2C测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "OLED硬件I2C测试失败" }
    $binary = Join-Path $build "test_oled_radio.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-sign-compare -Wno-missing-braces `
        -ITests/oled_mocks -ITests/mocks -IAPP -IBSP/OLED -IBSP/NRF24L01 -IBSP/KEY `
        -DOLED_TEXT_TEST_TIMING -DREMOTE_DIAGNOSTIC_ENABLED=0 Tests/test_oled_radio.c `
        APP/app_main.c APP/remote_arm3.c APP/remote_display.c APP/remote_diagnostic.c APP/remote_status_protocol.c `
        APP/remote_status_model.c BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c BSP/OLED/OLED.c BSP/OLED/OLED_Data.c BSP/NRF24L01/NRF24L01.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "OLED与无线真实驱动集成测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "OLED与无线真实驱动集成测试失败" }
    $binary = Join-Path $build "test_key.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/key_mocks -IBSP/KEY -IAPP -IBSP/OLED -DOLED_TEXT_LAYOUT_ONLY `
        Tests/test_key.c BSP/KEY/key.c APP/remote_display.c APP/remote_status_protocol.c APP/remote_status_model.c BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "真实按键扫描测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "真实按键扫描测试失败" }
    $binary = Join-Path $build "test_status.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP `
        Tests/test_status.c APP/remote_status_protocol.c APP/remote_status_model.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "状态模型测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "状态模型测试失败" }
    foreach ($legacy in @(0,1)) {
    $binary = Join-Path $build $(if ($legacy) { "test_display_legacy.exe" } else { "test_display.exe" })
    & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-sign-compare -Wno-missing-braces "-DOLED_ENABLE_LEGACY_FONTS=$legacy" -ITests/oled_mocks -IBSP/OLED -IAPP `
        Tests/test_display.c Tests/display_hal.c APP/remote_display.c APP/remote_status_protocol.c APP/remote_status_model.c `
        BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c BSP/OLED/OLED.c BSP/OLED/OLED_Data.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "显示测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "显示测试失败" }
    }
    $binary = Join-Path $build "test_bilingual.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-sign-compare -Wno-missing-braces -ITests/oled_mocks -IBSP/OLED -IAPP `
        Tests/test_bilingual.c Tests/english_reference.c Tests/display_hal.c APP/remote_display.c APP/remote_status_protocol.c APP/remote_status_model.c `
        BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c BSP/OLED/OLED.c BSP/OLED/OLED_Data.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "Bilingual differential test compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "Bilingual differential test failed" }
    $binary = Join-Path $build "test_diagnostic.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP -IBSP/OLED -ITests/mocks -DOLED_TEXT_LAYOUT_ONLY `
        Tests/test_diagnostic.c BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c APP/remote_display.c APP/remote_status_protocol.c `
        APP/remote_status_model.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "诊断页测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "诊断页测试失败" }
    $binary = Join-Path $build "test_ack_driver.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/mocks -IBSP/NRF24L01 `
        Tests/test_ack_driver.c BSP/NRF24L01/NRF24L01.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "ACK驱动测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "ACK驱动测试失败" }
    foreach ($diagnostic in @(0,1)) {
    $binary = Join-Path $build "test_app_$diagnostic.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/mocks -IAPP -IBSP/KEY -IBSP/NRF24L01 -IBSP/OLED -DOLED_TEXT_LAYOUT_ONLY `
        "-DREMOTE_DIAGNOSTIC_ENABLED=$diagnostic" `
        Tests/test_app.c APP/app_main.c APP/remote_arm3.c APP/remote_display.c BSP/OLED/OLED_Text.c BSP/OLED/OLED_ChineseFont.c `
        APP/remote_diagnostic.c APP/remote_status_protocol.c APP/remote_status_model.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "应用流程测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "应用流程测试失败" }
    }
    $binary = Join-Path $build "test_cross_encoder.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror `
        -DAPP_NRF_TELEMETRY_ENABLED=0 -DAPP_NRF_ACK_PAYLOAD_ENABLED=0 `
        "-I$MainProject/Protocol/CAN" "-I$MainProject/Tests/mocks" "-I$MainProject/APP" "-I$MainProject/Device" `
        "-I$MainProject/BSP/SYSTEM" "-I$MainProject/BSP/NRF24L01" -IAPP `
        Tests/test_cross_encoder.c APP/remote_status_protocol.c `
        "$MainProject/APP/nrf_status_telemetry.c" "$MainProject/APP/nrf_remote_protocol.c" -o $binary
    if ($LASTEXITCODE -ne 0) { throw "双工程编解码对照测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "双工程编解码对照测试失败" }
    if ($RenderPreview) {
        $snapshot = Join-Path $build "oled_snapshot.txt"
        & (Join-Path $build "test_display.exe") $snapshot
        if ($LASTEXITCODE -ne 0) { throw "预览快照生成失败" }
        $english = Join-Path $build "oled_english_snapshot.txt"
        & (Join-Path $build "test_bilingual.exe") $english
        if ($LASTEXITCODE -ne 0) { throw "English framebuffer generation failed" }
        $combined = Join-Path $build "oled_bilingual_snapshot.txt"
        [IO.File]::WriteAllBytes($combined,([IO.File]::ReadAllBytes($snapshot)+[IO.File]::ReadAllBytes($english)))
        & node Tests/render_oled_preview.js $combined (Join-Path $build "oled_preview.png")
        if ($LASTEXITCODE -ne 0) { throw "实际字模预览生成失败" }
        & python Tests/render_framebuffer_preview.py $combined (Join-Path $build "bilingual_previews")
        if ($LASTEXITCODE -ne 0) { throw "原尺寸及放大显存预览生成失败" }
    }
} finally {
    Pop-Location
}
