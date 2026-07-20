Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# CRC32 is implemented in C# to avoid signed 32-bit bitwise behavior in Windows PowerShell.
if (-not ("ProfileCrc32" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
public static class ProfileCrc32 {
    public static UInt32 Compute(byte[] data) {
        UInt32 crc = 0xFFFFFFFFu;
        foreach (byte value in data) {
            crc ^= value;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }
}
"@
}

$script:ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$script:DefaultJson = Join-Path $script:ProjectRoot "config\sensor_profiles.json"
$script:Serial = $null

function Add-Log([string]$Text) {
    $stamp = Get-Date -Format "HH:mm:ss"
    $logBox.AppendText("[$stamp] $Text`r`n")
    $logBox.SelectionStart = $logBox.TextLength
    $logBox.ScrollToCaret()
    [System.Windows.Forms.Application]::DoEvents()
}

function Refresh-Ports {
    $selected = $portCombo.Text
    $portCombo.Items.Clear()
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    foreach ($port in $ports) { [void]$portCombo.Items.Add($port) }
    if ($ports -contains $selected) {
        $portCombo.SelectedItem = $selected
    } elseif ($portCombo.Items.Count -gt 0) {
        $portCombo.SelectedIndex = $portCombo.Items.Count - 1
    }
    Add-Log ("Ports: " + (($ports -join ", ") -replace "^$", "none"))
}

function Load-JsonPreview([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $previewLabel.Text = "JSON file not found"
        return $null
    }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -eq 0 -or $bytes.Length -gt 65536) {
            throw "JSON size must be 1..65536 bytes"
        }
        $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
        $text = $utf8.GetString($bytes)
        $json = $text | ConvertFrom-Json
        if ($null -eq $json.schema_version -or $null -eq $json.config_version -or
            $null -eq $json.profiles) {
            throw "schema_version, config_version, or profiles is missing"
        }
        $pnList = @($json.profiles | ForEach-Object { $_.product_num }) -join ", "
        $previewLabel.Text = "Version: $($json.config_version)    Profiles: $(@($json.profiles).Count)`r`nPN: $pnList    Size: $($bytes.Length) bytes"
        # Unary comma prevents PowerShell from unrolling byte[] into Object[].
        return ,$bytes
    } catch {
        $previewLabel.Text = "Invalid JSON: $($_.Exception.Message)"
        return $null
    }
}

function Wait-Control([System.IO.Ports.SerialPort]$Port, [int]$Expected,
                      [int]$TimeoutMs = 3000) {
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $value = $Port.ReadByte()
            if ($value -eq $Expected) { return }
            if ($value -eq [int][char]'N') {
                $remainder = $Port.ReadLine().Trim()
                throw "Device rejected request: N$remainder"
            }
        } catch [System.TimeoutException] {
            [System.Windows.Forms.Application]::DoEvents()
        }
    }
    throw "Handshake timeout waiting for '$([char]$Expected)'"
}

function Set-UiEnabled([bool]$Enabled) {
    $portCombo.Enabled = $Enabled
    $refreshButton.Enabled = $Enabled
    $fileText.Enabled = $Enabled
    $browseButton.Enabled = $Enabled
    $uploadButton.Enabled = $Enabled
}

function Upload-Profile {
    $portName = $portCombo.Text.Trim()
    $jsonPath = $fileText.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($portName)) {
        [System.Windows.Forms.MessageBox]::Show("Select a COM port first.", "Profile Updater")
        return
    }
    $payload = Load-JsonPreview $jsonPath
    if ($null -eq $payload) {
        [System.Windows.Forms.MessageBox]::Show("Select a valid JSON file first.", "Profile Updater")
        return
    }

    Set-UiEnabled $false
    $progressBar.Value = 0
    $statusLabel.Text = "Opening $portName ..."
    Add-Log "Open $portName with DTR/RTS disabled"

    try {
        $crc = [ProfileCrc32]::Compute($payload)
        $magic = [System.Text.Encoding]::ASCII.GetBytes("ESPCFG1")
        $lengthBytes = [System.BitConverter]::GetBytes([uint32]$payload.Length)
        $crcBytes = [System.BitConverter]::GetBytes([uint32]$crc)
        $header = New-Object byte[] 15
        [System.Array]::Copy($magic, 0, $header, 0, 7)
        [System.Array]::Copy($lengthBytes, 0, $header, 7, 4)
        [System.Array]::Copy($crcBytes, 0, $header, 11, 4)

        $script:Serial = New-Object System.IO.Ports.SerialPort
        $script:Serial.PortName = $portName
        $script:Serial.BaudRate = 115200
        $script:Serial.DataBits = 8
        $script:Serial.Parity = [System.IO.Ports.Parity]::None
        $script:Serial.StopBits = [System.IO.Ports.StopBits]::One
        $script:Serial.Handshake = [System.IO.Ports.Handshake]::None
        $script:Serial.DtrEnable = $false
        $script:Serial.RtsEnable = $false
        $script:Serial.ReadTimeout = 200
        $script:Serial.WriteTimeout = 10000
        $script:Serial.NewLine = "`n"
        $script:Serial.Open()
        Start-Sleep -Milliseconds 100
        $script:Serial.DiscardInBuffer()

        $statusLabel.Text = "Send header ..."
        $script:Serial.Write($header, 0, $header.Length)
        Wait-Control $script:Serial ([int][char]'R')
        Add-Log ("Header accepted, length={0}, CRC32=0x{1:X8}" -f $payload.Length, $crc)

        $offset = 0
        while ($offset -lt $payload.Length) {
            $count = [Math]::Min(64, $payload.Length - $offset)
            $script:Serial.Write($payload, $offset, $count)
            Wait-Control $script:Serial ([int][char]'P')
            $offset += $count
            $percent = [Math]::Min(100, [int](100.0 * $offset / $payload.Length))
            $progressBar.Value = $percent
            $statusLabel.Text = "Uploading: $offset / $($payload.Length) bytes"
            [System.Windows.Forms.Application]::DoEvents()
        }

        $script:Serial.ReadTimeout = 15000
        $response = $script:Serial.ReadLine().Trim()
        Add-Log "Device response: $response"
        if ($response -ne "ACK") { throw "Upload failed: $response" }

        $progressBar.Value = 100
        $statusLabel.Text = "Success - device is restarting"
        [System.Windows.Forms.MessageBox]::Show(
            "Profile installed successfully. The device is restarting.",
            "Profile Updater", [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information)
    } catch {
        $statusLabel.Text = "Failed"
        Add-Log "ERROR: $($_.Exception.Message)"
        [System.Windows.Forms.MessageBox]::Show(
            $_.Exception.Message, "Upload failed",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error)
    } finally {
        if ($null -ne $script:Serial) {
            if ($script:Serial.IsOpen) { $script:Serial.Close() }
            $script:Serial.Dispose()
            $script:Serial = $null
        }
        Set-UiEnabled $true
    }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "ESP32 Sensor Profile Updater"
$form.StartPosition = "CenterScreen"
$form.ClientSize = New-Object System.Drawing.Size(720, 540)
$form.MinimumSize = New-Object System.Drawing.Size(736, 579)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

$title = New-Object System.Windows.Forms.Label
$title.Text = "ESP32 Sensor Profile Hot Update"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
$title.Location = New-Object System.Drawing.Point(20, 18)
$title.AutoSize = $true
$form.Controls.Add($title)

$instruction = New-Object System.Windows.Forms.Label
$instruction.Text = "1. On device: SYSTEM INFO > CONFIG UPLOAD    2. Select COM and JSON    3. Click Upload"
$instruction.Location = New-Object System.Drawing.Point(22, 58)
$instruction.Size = New-Object System.Drawing.Size(675, 25)
$form.Controls.Add($instruction)

$portLabel = New-Object System.Windows.Forms.Label
$portLabel.Text = "COM port"
$portLabel.Location = New-Object System.Drawing.Point(22, 102)
$portLabel.AutoSize = $true
$form.Controls.Add($portLabel)

$portCombo = New-Object System.Windows.Forms.ComboBox
$portCombo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
$portCombo.Location = New-Object System.Drawing.Point(110, 98)
$portCombo.Size = New-Object System.Drawing.Size(180, 30)
$form.Controls.Add($portCombo)

$refreshButton = New-Object System.Windows.Forms.Button
$refreshButton.Text = "Refresh"
$refreshButton.Location = New-Object System.Drawing.Point(305, 96)
$refreshButton.Size = New-Object System.Drawing.Size(95, 32)
$refreshButton.Add_Click({ Refresh-Ports })
$form.Controls.Add($refreshButton)

$fileLabel = New-Object System.Windows.Forms.Label
$fileLabel.Text = "JSON file"
$fileLabel.Location = New-Object System.Drawing.Point(22, 151)
$fileLabel.AutoSize = $true
$form.Controls.Add($fileLabel)

$fileText = New-Object System.Windows.Forms.TextBox
$fileText.Location = New-Object System.Drawing.Point(110, 147)
$fileText.Size = New-Object System.Drawing.Size(475, 30)
$fileText.Text = $script:DefaultJson
$fileText.Add_Leave({ [void](Load-JsonPreview $fileText.Text) })
$form.Controls.Add($fileText)

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = "Browse..."
$browseButton.Location = New-Object System.Drawing.Point(598, 145)
$browseButton.Size = New-Object System.Drawing.Size(98, 32)
$browseButton.Add_Click({
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*"
    $dialog.FileName = $fileText.Text
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $fileText.Text = $dialog.FileName
        [void](Load-JsonPreview $fileText.Text)
    }
})
$form.Controls.Add($browseButton)

$previewLabel = New-Object System.Windows.Forms.Label
$previewLabel.Location = New-Object System.Drawing.Point(110, 187)
$previewLabel.Size = New-Object System.Drawing.Size(585, 48)
$previewLabel.ForeColor = [System.Drawing.Color]::DarkSlateGray
$form.Controls.Add($previewLabel)

$uploadButton = New-Object System.Windows.Forms.Button
$uploadButton.Text = "Upload Profile"
$uploadButton.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$uploadButton.Location = New-Object System.Drawing.Point(22, 246)
$uploadButton.Size = New-Object System.Drawing.Size(180, 42)
$uploadButton.Add_Click({ Upload-Profile })
$form.Controls.Add($uploadButton)

$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready"
$statusLabel.Location = New-Object System.Drawing.Point(220, 254)
$statusLabel.Size = New-Object System.Drawing.Size(475, 25)
$form.Controls.Add($statusLabel)

$progressBar = New-Object System.Windows.Forms.ProgressBar
$progressBar.Location = New-Object System.Drawing.Point(22, 303)
$progressBar.Size = New-Object System.Drawing.Size(674, 25)
$form.Controls.Add($progressBar)

$logLabel = New-Object System.Windows.Forms.Label
$logLabel.Text = "Log"
$logLabel.Location = New-Object System.Drawing.Point(22, 347)
$logLabel.AutoSize = $true
$form.Controls.Add($logLabel)

$logBox = New-Object System.Windows.Forms.TextBox
$logBox.Location = New-Object System.Drawing.Point(22, 372)
$logBox.Size = New-Object System.Drawing.Size(674, 140)
$logBox.Multiline = $true
$logBox.ReadOnly = $true
$logBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
$logBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$form.Controls.Add($logBox)

$form.Add_FormClosing({
    if ($null -ne $script:Serial -and $script:Serial.IsOpen) {
        $script:Serial.Close()
    }
})

Refresh-Ports
[void](Load-JsonPreview $fileText.Text)
[void]$form.ShowDialog()
