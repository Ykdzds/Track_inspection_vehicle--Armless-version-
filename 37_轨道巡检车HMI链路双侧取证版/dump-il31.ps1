param([string]$TypeName='Visual_PIX.Modules.Home.ViewModels.DebugViewModel',[string]$MethodName='FrBhSaUqTjnyNmVndcZbfgAVvLDj')
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$t=$vp.GetType($TypeName)
if($MethodName -eq 'SQGJjlnqLGCrBFyvDWrQAFlrwHaOA'){$t=$vp.GetTypes()|Where-Object {$_.GetMethods($flags).Name -contains $MethodName}|Select-Object -First 1}
Write-Output $t.FullName
# Force runtime method bodies to decrypt before obtaining IL.
[Runtime.CompilerServices.RuntimeHelpers]::RunClassConstructor($t.TypeHandle)
if($MethodName -eq 'SQGJjlnqLGCrBFyvDWrQAFlrwHaOA'){
 $c=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.TextCtrl'))
 try{$t.GetMethod($MethodName,$flags).Invoke($null,[object[]]@($c,[byte]0,[int]0))}catch{Write-Output $_.Exception.Message}
}
$ops=@{};[Reflection.Emit.OpCodes].GetFields()|ForEach-Object {$o=$_.GetValue($null);$ops[[int]$o.Value]=$o}
foreach($m in $t.GetMethods($flags)|Where-Object Name -eq $MethodName){
 $il=$m.GetMethodBody().GetILAsByteArray();$i=0
 while($i -lt $il.Length){$at=$i;$n=[int]$il[$i++];if($n -eq 254){$n=[int](-512+[int]$il[$i++])};$op=$ops[$n];if(!$op){Write-Output ('bad '+$at+' '+$n);break};$operand=''
 switch([string]$op.OperandType){
 'InlineNone'{} 'ShortInlineI'{$operand=$il[$i];$i++} 'ShortInlineVar'{$operand=$il[$i];$i++} 'InlineVar'{$operand=[BitConverter]::ToUInt16($il,$i);$i+=2}
 'InlineI'{$operand=[BitConverter]::ToInt32($il,$i);$i+=4} 'InlineI8'{$operand=[BitConverter]::ToInt64($il,$i);$i+=8}
 'ShortInlineR'{$operand=[BitConverter]::ToSingle($il,$i);$i+=4} 'InlineR'{$operand=[BitConverter]::ToDouble($il,$i);$i+=8}
 'InlineSwitch'{$count=[BitConverter]::ToInt32($il,$i);$i+=4;$operand=$count;$i+=$count*4}
 'ShortInlineBrTarget'{$operand=[int]$il[$i];if($operand -gt 127){$operand-=256};$i++}
 'InlineBrTarget'{$operand=[BitConverter]::ToInt32($il,$i);$i+=4}
 default{$token=[BitConverter]::ToInt32($il,$i);$i+=4;try{if([string]$op.OperandType -eq 'InlineString'){$operand=$m.Module.ResolveString($token)}else{$operand=$m.Module.ResolveMember($token)}}catch{$operand=('token '+$token)}}
 }
 Write-Output ('{0:X4} {1} {2}' -f $at,$op.Name,$operand)
 }
}
