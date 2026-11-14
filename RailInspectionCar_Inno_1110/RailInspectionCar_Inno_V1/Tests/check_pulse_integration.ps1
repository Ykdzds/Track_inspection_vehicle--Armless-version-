$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$tim = Get-Content -LiteralPath (Join-Path $root "Core/Src/tim.c") -Raw -Encoding UTF8
$irq = Get-Content -LiteralPath (Join-Path $root "Core/Src/stm32f4xx_it.c") -Raw -Encoding UTF8
$header = Get-Content -LiteralPath (Join-Path $root "Core/Inc/stm32f4xx_it.h") -Raw -Encoding UTF8
$ioc = Get-Content -LiteralPath (Join-Path $root "RailInspectionCar_V1.ioc") -Raw -Encoding UTF8

# 检查真实中断、NVIC与受保护的上电低电平配置，防止重新生成代码后静默回归。
foreach ($axis in @(
    @{ Timer = "TIM9"; Vector = "TIM1_BRK_TIM9"; Port = "GPIOE"; Pin = "GPIO_PIN_5"; Call = "servo_pulse_update_tim9_isr" },
    @{ Timer = "TIM11"; Vector = "TIM1_TRG_COM_TIM11"; Port = "GPIOB"; Pin = "GPIO_PIN_9"; Call = "servo_pulse_update_tim11_isr" }
)) {
    $vector = $axis.Vector
    $timer = $axis.Timer
    $checks = @(
        @{ Text = $ioc; Pattern = "(?m)^NVIC\.$($vector)_IRQn=true\\:4\\:0\\:" },
        @{ Text = $tim; Pattern = "HAL_NVIC_SetPriority\($($vector)_IRQn,\s*4,\s*0\)" },
        @{ Text = $tim; Pattern = "HAL_NVIC_EnableIRQ\($($vector)_IRQn\)" },
        @{ Text = $tim; Pattern = "HAL_NVIC_DisableIRQ\($($vector)_IRQn\)" },
        @{ Text = $header; Pattern = "void $($vector)_IRQHandler\(void\);" },
        @{ Text = $irq; Pattern = "(?s)void $($vector)_IRQHandler\(void\)\s*\{[^}]*USER CODE BEGIN $($vector)_IRQn 0[^}]*$($axis.Call)\(\);[^}]*USER CODE END $($vector)_IRQn 0" },
        @{ Text = $tim; Pattern = "(?s)USER CODE BEGIN $($timer)_MspPostInit 1.*?HAL_GPIO_WritePin\($($axis.Port), $($axis.Pin), GPIO_PIN_RESET\);.*?GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;.*?HAL_GPIO_Init\($($axis.Port), &GPIO_InitStruct\);.*?USER CODE END $($timer)_MspPostInit 1" }
    )
    foreach ($check in $checks) {
        if (![regex]::IsMatch($check.Text, $check.Pattern)) {
            throw "Pulse integration check failed: $timer / $($check.Pattern)"
        }
    }
}
Write-Output "Pulse integration checks passed: TIM9/TIM11 IRQ, NVIC, protected idle GPIO."
