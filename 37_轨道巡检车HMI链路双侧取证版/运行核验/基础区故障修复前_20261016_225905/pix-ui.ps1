param([string]$Action='Inspect',[string]$Name,[string]$Value,[int]$X,[int]$Y,[int]$EndX,[int]$EndY,[string]$Keys)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms
if(-not ('ControlPixInput' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class ControlPixInput {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetPhysicalCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
}
'@
}
[ControlPixInput]::SetProcessDPIAware() | Out-Null
[ControlPixInput]::SetThreadDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null
$p=Get-Process Visual-PIX | Where-Object { $_.MainWindowTitle -match '13_轨道巡检车UART5共同目标转速控制版' } | Select-Object -First 1
if($p.MainWindowTitle -notmatch '13_轨道巡检车UART5共同目标转速控制版'){throw '只允许修改13独立副本'}
$w=[System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
function Nodes { $w.FindAll('Descendants',[System.Windows.Automation.Condition]::TrueCondition) }
function Props { @(Nodes | Where-Object {$_.Current.Name -eq 'Xceed.Wpf.Toolkit.PropertyGrid.PropertyItem'}) }
function Click($x,$y) {
    [ControlPixInput]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    [ControlPixInput]::SetPhysicalCursorPos($x,$y) | Out-Null
    [ControlPixInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    [ControlPixInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}
function SelectControl($name) {
    # 右侧控件树可能位于屏幕X=2000以外，不能用固定屏幕宽度过滤真实控件。
    $visible=Nodes | Where-Object {$_.Current.Name -eq $name -and $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Text -and !$_.Current.BoundingRectangle.IsEmpty -and $_.Current.BoundingRectangle.Y -gt 700} | Select-Object -First 1
    if($visible){
        $r=$visible.Current.BoundingRectangle
        Click ([int]($r.X+$r.Width/2)) ([int]($r.Y+$r.Height/2))
        $values=@(Props | ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
        if($values -contains $name){return}
    }
    $treeItem=Nodes | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and
        $_.Current.Name -match ('^\d+\s+'+[regex]::Escape($name)+'\s+') -and
        $_.GetSupportedPatterns().ProgrammaticName -contains 'SelectionItemPatternIdentifiers.Pattern'
    } | Select-Object -First 1
    if($treeItem){
        $treeItem.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 200
        $rows=Props
        $values=@($rows | ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
        if($values -contains $name){return}
    }
    $combo=Nodes | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ComboBox -and !$_.Current.BoundingRectangle.IsEmpty} | Sort-Object {$_.Current.BoundingRectangle.Y} | Select-Object -First 1
    $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    Start-Sleep -Milliseconds 100
    $item=Nodes | Where-Object {$_.Current.Name -match ('^\d+\s+'+[regex]::Escape($name)+'\s+') -and $_.Current.ControlType -in @([System.Windows.Automation.ControlType]::ListItem,[System.Windows.Automation.ControlType]::DataItem,[System.Windows.Automation.ControlType]::Custom) -and $_.GetSupportedPatterns().ProgrammaticName -contains 'SelectionItemPatternIdentifiers.Pattern'} | Select-Object -First 1
    if(!$item){throw "找不到控件：$name"}
    $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 200
    $rows=Props
    $values=@($rows | ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
    if($values -notcontains $name){throw "控件选择未生效：目标=$name"}
}
function SelectPage($name) {
    $item=Nodes | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::DataItem -and
        $_.GetSupportedPatterns().ProgrammaticName -contains 'SelectionItemPatternIdentifiers.Pattern' -and
        $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::NameProperty,$name))
    } | Select-Object -First 1
    if (!$item) {
        $item=Nodes | Where-Object {$_.Current.Name -eq $name -and $_.Current.ControlType -in @([System.Windows.Automation.ControlType]::ListItem,[System.Windows.Automation.ControlType]::Custom) -and $_.GetSupportedPatterns().ProgrammaticName -contains 'SelectionItemPatternIdentifiers.Pattern'} | Select-Object -First 1
    }
    if(!$item){throw "找不到页面：$name"}
    $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 100
}
function SetRow($index,$value) {
    $a=Props
    $e=$a[$index].FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Edit))
    if(!$e){throw "属性$index没有编辑器"}
    $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue([string]$value)
    [System.Windows.Forms.SendKeys]::SendWait('{TAB}')
}
switch($Action) {
    'SetMany' {
        $pairs=$Value | ConvertFrom-Json
        foreach($pair in $pairs){SetRow ([int]$pair[0]) ([string]$pair[1])}
    }
    'Font' {
        $property=(Props)[$X]
        $pattern=$property.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        $pattern.SetValue($Value)
    }
    'Check' {
        $check=(Props)[$X].FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::CheckBox))
        $pattern=$check.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
        $desired=if($Value -eq 'True'){'On'}else{'Off'}
        if($pattern.Current.ToggleState.ToString() -ne $desired){$pattern.Toggle()}
    }
    'Tree' {Nodes | ForEach-Object {'{0}|{1}|{2}' -f $_.Current.ControlType.ProgrammaticName,$_.Current.Name,$_.Current.BoundingRectangle}}
    'Click' { Click $X $Y }
    'Drag' {
        [ControlPixInput]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
        [ControlPixInput]::SetPhysicalCursorPos($X,$Y) | Out-Null
        [ControlPixInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 100
        for($i=1;$i -le 10;$i++) {
            [ControlPixInput]::SetPhysicalCursorPos([int]($X+($EndX-$X)*$i/10),[int]($Y+($EndY-$Y)*$i/10)) | Out-Null
            Start-Sleep -Milliseconds 30
        }
        [ControlPixInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 250
    }
    'DoubleClick' { Click $X $Y; Click $X $Y }
    'Select' { SelectControl $Name }
    'SelectPage' { SelectPage $Name }
    'Page' { SelectPage $Name }
    'SetRow' { SetRow $X $Value }
    'SetGlobal' {
        SelectControl $Name
        $row=(Props)[1]
        $check=$row.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::CheckBox))
        if(!$check){throw "控件$name没有global复选框"}
        $toggle=$check.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
        if($toggle.Current.ToggleState -ne [System.Windows.Automation.ToggleState]::On){$toggle.Toggle()}
        if($toggle.Current.ToggleState -ne [System.Windows.Automation.ToggleState]::On){throw "global读回失败：$name"}
    }
    'SetText' {
        foreach($pair in @(@(2,$Name),@(4,$X),@(5,$Y),@(6,$Value),@(7,$EndX),@(14,$Keys))){SetRow $pair[0] $pair[1]}
        [System.Windows.Forms.SendKeys]::SendWait('^s')
    }
    'Keys' { [ControlPixInput]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; [System.Windows.Forms.SendKeys]::SendWait($Value) }
    'Event' {
        $doc=Nodes | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'} | Sort-Object {$_.Current.BoundingRectangle.X} | Select-Object -Last 1
        if(!$doc){throw '当前没有可编辑的事件文档，请先选择页面事件标签。'}
        $vp=$doc.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        $vp.SetValue($Value.Replace("`r`n","`n"))
        if($vp.Current.Value.TrimEnd() -ne $Value.Replace("`r`n","`n").TrimEnd()){throw '事件读回失败'}
    }
    'Inspect' { $i=0; Props | ForEach-Object {"$i $($_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value)"; $i++} }
}
