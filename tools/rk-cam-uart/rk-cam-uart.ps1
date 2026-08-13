Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$script:port = $null
$script:rx = ''
$script:pending = $null
$script:req = 0
$script:requestTimeoutSec = 45

$form = New-Object Windows.Forms.Form
$form.Text = 'RK3588 Camera UART 控制台'
$form.Size = New-Object Drawing.Size(1120, 720)
$form.MinimumSize = New-Object Drawing.Size(900, 600)
$form.StartPosition = 'CenterScreen'

$font = New-Object Drawing.Font('Microsoft YaHei UI', 9)
$form.Font = $font

function New-Label([string]$text, [int]$x, [int]$y, [int]$w = 90) {
  $c = New-Object Windows.Forms.Label
  $c.Text = $text; $c.Location = New-Object Drawing.Point($x,$y); $c.Size = New-Object Drawing.Size($w,26)
  $form.Controls.Add($c); return $c
}
function New-Button([string]$text, [int]$x, [int]$y, [int]$w = 100, $parent = $form) {
  $c = New-Object Windows.Forms.Button
  $c.Text = $text; $c.Location = New-Object Drawing.Point($x,$y); $c.Size = New-Object Drawing.Size($w,30)
  $parent.Controls.Add($c); return $c
}

New-Label '串口' 18 18 45
$portBox = New-Object Windows.Forms.ComboBox
$portBox.Location = New-Object Drawing.Point(65,14); $portBox.Size = New-Object Drawing.Size(90,30); $portBox.DropDownStyle = 'DropDownList'
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object { [void]$portBox.Items.Add($_) }
if ($portBox.Items.Count -gt 0) { $portBox.SelectedItem = if ($portBox.Items.Contains('COM3')) { 'COM3' } else { $portBox.Items[0] } }
$form.Controls.Add($portBox)
New-Label '波特率' 170 18 55
$baudBox = New-Object Windows.Forms.ComboBox
$baudBox.Location = New-Object Drawing.Point(225,14); $baudBox.Size = New-Object Drawing.Size(100,30); $baudBox.DropDownStyle = 'DropDownList'
@('1500000','921600','115200') | ForEach-Object { [void]$baudBox.Items.Add($_) }; $baudBox.SelectedIndex = 0; $form.Controls.Add($baudBox)
$connectBtn = New-Button '连接' 345 14 90
$recoverBtn = New-Button '恢复 console' 445 14 110
$statusLabel = New-Label '未连接' 570 18 300
$statusLabel.ForeColor = [Drawing.Color]::Maroon

$split = New-Object Windows.Forms.SplitContainer
$split.Location = New-Object Drawing.Point(18,58); $split.Size = New-Object Drawing.Size(1065,300); $split.SplitterDistance = 600
$split.Anchor = 'Top,Left,Right'; $form.Controls.Add($split)

$grid = New-Object Windows.Forms.DataGridView
$grid.Dock = 'Fill'; $grid.ReadOnly = $true; $grid.AllowUserToAddRows = $false; $grid.AutoSizeColumnsMode = 'Fill'
@('序号','序列号/ID','型号','固件','能力') | ForEach-Object { [void]$grid.Columns.Add($_, $_) }
$split.Panel1.Controls.Add($grid)

$cmdPanel = New-Object Windows.Forms.Panel; $cmdPanel.Dock = 'Fill'; $split.Panel2.Controls.Add($cmdPanel)
$cmdLabel = New-Object Windows.Forms.Label; $cmdLabel.Text = '相机指令'; $cmdLabel.Location = New-Object Drawing.Point(12,10); $cmdLabel.Size = New-Object Drawing.Size(120,25); $cmdPanel.Controls.Add($cmdLabel)
$commands = @('CAMERA_LIST_DEVICES','CAMERA_INIT','CAMERA_GET_STATUS','CAMERA_GET_CAPTURE_STATUS','CAMERA_GET_BATTERY','CAMERA_GET_STORAGE','CAMERA_TAKE_PHOTO','CAMERA_VIDEO_START','CAMERA_VIDEO_STOP','CAMERA_LIST_MEDIA','CAMERA_SET_MODE','CAMERA_SET_PARAM','CAMERA_GET_PARAM')
$cmdCombo = New-Object Windows.Forms.ComboBox; $cmdCombo.Location = New-Object Drawing.Point(12,40); $cmdCombo.Size = New-Object Drawing.Size(260,28); $cmdCombo.DropDownStyle = 'DropDownList'
$commands | ForEach-Object { [void]$cmdCombo.Items.Add($_) }; $cmdCombo.SelectedIndex = 0; $cmdPanel.Controls.Add($cmdCombo)
$serialLabel = New-Object Windows.Forms.Label; $serialLabel.Text = '目标相机'; $serialLabel.Location = New-Object Drawing.Point(12,78); $serialLabel.Size = New-Object Drawing.Size(160,25); $cmdPanel.Controls.Add($serialLabel)
$cameraBox = New-Object Windows.Forms.ComboBox; $cameraBox.Location = New-Object Drawing.Point(12,105); $cameraBox.Size = New-Object Drawing.Size(360,28); $cameraBox.DropDownStyle = 'DropDownList'; $cameraBox.DisplayMember = 'Display'; [void]$cameraBox.Items.Add([pscustomobject]@{ Display='自动选择（不指定序列号）'; Serial='' }); $cameraBox.SelectedIndex = 0; $cmdPanel.Controls.Add($cameraBox)
$argsLabel = New-Object Windows.Forms.Label; $argsLabel.Text = '附加参数（可选）'; $argsLabel.Location = New-Object Drawing.Point(12,138); $argsLabel.Size = New-Object Drawing.Size(160,25); $cmdPanel.Controls.Add($argsLabel)
$argsBox = New-Object Windows.Forms.TextBox; $argsBox.Location = New-Object Drawing.Point(12,163); $argsBox.Size = New-Object Drawing.Size(260,28); $cmdPanel.Controls.Add($argsBox)
$sendBtn = New-Button '发送指令' 12 198 110 -parent $cmdPanel
$refreshBtn = New-Button '刷新相机' 135 198 110 -parent $cmdPanel
$customLabel = New-Object Windows.Forms.Label; $customLabel.Text = '自定义 shell（高级）'; $customLabel.Location = New-Object Drawing.Point(12,235); $customLabel.Size = New-Object Drawing.Size(180,25); $cmdPanel.Controls.Add($customLabel)
$customBox = New-Object Windows.Forms.TextBox; $customBox.Location = New-Object Drawing.Point(12,260); $customBox.Size = New-Object Drawing.Size(260,28); $cmdPanel.Controls.Add($customBox)
$customBtn = New-Button '执行 shell' 282 258 100 -parent $cmdPanel

$log = New-Object Windows.Forms.RichTextBox
$log.Location = New-Object Drawing.Point(18,375); $log.Size = New-Object Drawing.Size(1065,280); $log.ReadOnly = $true; $log.BackColor = [Drawing.Color]::FromArgb(25,30,35); $log.ForeColor = [Drawing.Color]::White; $log.Font = New-Object Drawing.Font('Consolas',9); $log.Anchor = 'Top,Bottom,Left,Right'; $form.Controls.Add($log)

function Add-Log([string]$line) {
  $stamp = (Get-Date).ToString('HH:mm:ss')
  $log.AppendText("[$stamp] $line`r`n"); $log.ScrollToCaret()
}
function Add-Raw([string]$text) {
  if (-not $text) { return }
  $clean = $text -replace "`e\[[0-9;?]*[ -/]*[@-~]", ''
  $log.AppendText($clean); $log.ScrollToCaret()
}
function Set-Connected([bool]$yes) {
  $statusLabel.Text = if ($yes) { "已连接 $($script:port.PortName)" } else { '未连接' }
  $statusLabel.ForeColor = if ($yes) { [Drawing.Color]::DarkGreen } else { [Drawing.Color]::Maroon }
  $connectBtn.Text = if ($yes) { '断开' } else { '连接' }
}
function Close-Port {
  if ($script:port) { try { $script:port.Close(); $script:port.Dispose() } catch {} ; $script:port = $null }
  Set-Connected $false
}
function Start-Request([string]$shell, [scriptblock]$done = $null, [bool]$wrapMarker = $true) {
  if (-not $script:port -or -not $script:port.IsOpen) { [Windows.Forms.MessageBox]::Show('请先连接串口。','提示'); return }
  if ($script:pending) { [Windows.Forms.MessageBox]::Show('上一条指令仍在等待反馈。','提示'); return }
  $script:req++
  $marker = "RKCAM_UART_DONE_$($script:req)_$([DateTime]::UtcNow.Ticks)"
  $script:rx = ''; $script:pending = [pscustomobject]@{ Marker=$marker; Done=$done; Started=(Get-Date) }
  $markerLeft = $marker.Substring(0, [Math]::Floor($marker.Length / 2))
  $markerRight = $marker.Substring($markerLeft.Length)
  $line = if ($wrapMarker) { "${shell}; printf '\n%s%s\n' '$markerLeft' '$markerRight'`n" } else { "${shell}`n" }
  try { $script:port.Write($line); Add-Log "> $shell" } catch { Add-Log "发送失败: $($_.Exception.Message)"; $script:pending=$null }
}
function Get-JsonValue($object, [string]$name) {
  $property = $object.PSObject.Properties[$name]
  if ($null -eq $property -or $null -eq $property.Value) { return '' }
  return [string]$property.Value
}
function Parse-DeviceList([string]$text) {
  $countPos = $text.IndexOf('"camera_count"')
  $start = if ($countPos -ge 0) { $text.LastIndexOf('{', $countPos) } else { $text.LastIndexOf('{') }
  $end = $text.LastIndexOf('}')
  if ($start -lt 0 -or $end -le $start) { Add-Log '未找到设备 JSON，请查看原始反馈。'; return }
  try {
    $obj = $text.Substring($start, $end-$start+1) | ConvertFrom-Json
    $previousSerial = if ($cameraBox.SelectedItem) { [string]$cameraBox.SelectedItem.Serial } else { '' }
    $grid.Rows.Clear(); $i=0
    $cameraBox.Items.Clear()
    [void]$cameraBox.Items.Add([pscustomobject]@{ Display='自动选择（不指定序列号）'; Serial='' })
    foreach ($cam in @($obj.cameras)) {
      $serial = Get-JsonValue $cam 'serial'
      $model = Get-JsonValue $cam 'model_key'
      if (-not $model) { $model = Get-JsonValue $cam 'camera_type' }
      if (-not $model) { $model = Get-JsonValue $cam 'camera_name' }
      $firmware = Get-JsonValue $cam 'firmware_version'
      $capabilities = Get-JsonValue $cam 'capabilities'
      [void]$grid.Rows.Add($i, $serial, $model, $firmware, $capabilities); $i++
      $displayModel = if ($model) { $model } else { '未知型号' }
      [void]$cameraBox.Items.Add([pscustomobject]@{ Display="$displayModel | $serial"; Serial=$serial })
    }
    $cameraBox.SelectedIndex = 0
    for ($index = 0; $index -lt $cameraBox.Items.Count; $index++) {
      if ([string]$cameraBox.Items[$index].Serial -eq $previousSerial -and $previousSerial) { $cameraBox.SelectedIndex = $index; break }
    }
    if (-not $previousSerial -and $cameraBox.Items.Count -gt 1) { $cameraBox.SelectedIndex = 1 }
    Add-Log "设备列表已更新，共 $($obj.camera_count) 台。"
  } catch { Add-Log "设备 JSON 解析失败: $($_.Exception.Message)" }
}

$timer = New-Object Windows.Forms.Timer; $timer.Interval = 100
$timer.Add_Tick({
  if (-not $script:port -or -not $script:port.IsOpen) { return }
  if ($script:port.BytesToRead -gt 0) { $chunk=$script:port.ReadExisting(); $script:rx += $chunk; Add-Raw $chunk }
  if ($script:pending -and ((Get-Date) - $script:pending.Started).TotalSeconds -gt $script:requestTimeoutSec) {
    Add-Log "等待板端反馈超时（$($script:requestTimeoutSec) 秒）。请检查 service 日志和相机 USB 状态。"
    $script:pending = $null
  }
  if ($script:pending -and $script:rx.Contains($script:pending.Marker)) {
    $p = $script:pending; $script:pending=$null
    $clean = $script:rx -replace "`e\[[0-9;?]*[ -/]*[@-~]", ''
    if ($p.Done) { & $p.Done $clean }
  }
})
$timer.Start()

$connectBtn.Add_Click({
  if ($script:port -and $script:port.IsOpen) { Close-Port; return }
  try {
    $script:port = New-Object System.IO.Ports.SerialPort($portBox.Text, [int]$baudBox.Text, 'None', 8, 'One')
    $script:port.Handshake = 'None'; $script:port.ReadTimeout=100; $script:port.WriteTimeout=3000; $script:port.Open(); Set-Connected $true; Add-Log '串口已打开。'
  } catch { Close-Port; Add-Log "连接失败: $($_.Exception.Message)" }
})
$recoverBtn.Add_Click({
  if (-not $script:port -or -not $script:port.IsOpen) { [Windows.Forms.MessageBox]::Show('请先连接串口。','提示'); return }
  try { $script:port.Write("`nconsole`n"); Add-Log '已发送 console；等待 Linux 提示符。' } catch { Add-Log "发送失败: $($_.Exception.Message)" }
})
$sendBtn.Add_Click({
  $serial = if ($cameraBox.SelectedItem) { ([string]$cameraBox.SelectedItem.Serial).Trim() } else { '' }; $suffix = if ($serial) { " camera_serial=$serial" } else { '' }
  if ($serial -and $serial -notmatch '^[A-Za-z0-9._:-]+$') { [Windows.Forms.MessageBox]::Show('序列号包含不允许的字符。','参数错误'); return }
  $extra = $argsBox.Text.Trim()
  if ($extra -and $extra -notmatch '^[A-Za-z0-9_./:=,@+ -]+$') { [Windows.Forms.MessageBox]::Show('附加参数只能包含字母、数字、空格和常用参数符号。','参数错误'); return }
  if ($extra) { $suffix += " $extra" }
  $journal = "; journalctl -u youyeetoo-app.service -n 80 --no-pager 2>/dev/null | grep 'last_command=$($cmdCombo.Text)' | tail -n 2"
  $readback = if ($cmdCombo.Text -eq 'CAMERA_LIST_DEVICES') { "; sleep 8$journal; cat /var/opt/youyeetoo/runtime/camera_device_list.json 2>/dev/null || cat /mnt/userdata/youyeetoo/runtime/camera_device_list.json 2>/dev/null" } else { "; sleep 8$journal" }
  $callback = if ($cmdCombo.Text -eq 'CAMERA_LIST_DEVICES') { { param($response) Parse-DeviceList $response } } else { $null }
  Start-Request ("printf '%s\n' '$($cmdCombo.Text)$suffix' >> /var/opt/youyeetoo/runtime/telemetry_command_request.txt$readback") $callback
})
$refreshBtn.Add_Click({
  Start-Request "printf '%s\n' 'CAMERA_LIST_DEVICES' >> /var/opt/youyeetoo/runtime/telemetry_command_request.txt; sleep 8; journalctl -u youyeetoo-app.service -n 80 --no-pager 2>/dev/null | grep 'last_command=CAMERA_LIST_DEVICES' | tail -n 2; cat /var/opt/youyeetoo/runtime/camera_device_list.json 2>/dev/null || cat /mnt/userdata/youyeetoo/runtime/camera_device_list.json 2>/dev/null" { param($response) Parse-DeviceList $response }
})
$customBtn.Add_Click({ if ($customBox.Text.Trim()) { Start-Request $customBox.Text.Trim() } })
$grid.Add_SelectionChanged({
  if ($grid.SelectedRows.Count -gt 0) {
    $selectedSerial = [string]$grid.SelectedRows[0].Cells[1].Value
    for ($index = 0; $index -lt $cameraBox.Items.Count; $index++) {
      if ([string]$cameraBox.Items[$index].Serial -eq $selectedSerial) { $cameraBox.SelectedIndex = $index; break }
    }
  }
})
$form.Add_FormClosing({ Close-Port; $timer.Stop() })
Add-Log '提示：先选择 COM3 并连接；相机查询由板端 CameraSDK worker 执行。'
[void]$form.ShowDialog()
