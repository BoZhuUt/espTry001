using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new UpdaterForm());
    }
}

internal sealed class UpdaterForm : Form
{
    private const int MaxJsonSize = 64 * 1024;
    private const int ChunkSize = 64;
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("ESPCFG1");

    private readonly ComboBox portCombo = new ComboBox();
    private readonly Button refreshButton = new Button();
    private readonly TextBox fileText = new TextBox();
    private readonly Button browseButton = new Button();
    private readonly Label previewLabel = new Label();
    private readonly Button uploadButton = new Button();
    private readonly Label statusLabel = new Label();
    private readonly ProgressBar progressBar = new ProgressBar();
    private readonly TextBox logBox = new TextBox();
    private SerialPort serial;

    internal UpdaterForm()
    {
        Text = "ESP32 Sensor Profile Updater";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(720, 540);
        MinimumSize = new Size(736, 579);
        Font = new Font("Segoe UI", 10F);

        Label title = new Label();
        title.Text = "ESP32 Sensor Profile Hot Update";
        title.Font = new Font("Segoe UI", 16F, FontStyle.Bold);
        title.Location = new Point(20, 18);
        title.AutoSize = true;
        Controls.Add(title);

        Label instruction = new Label();
        instruction.Text = "1. Device: SYSTEM INFO > CONFIG UPLOAD    2. Select COM and JSON    3. Upload";
        instruction.Location = new Point(22, 58);
        instruction.Size = new Size(675, 25);
        Controls.Add(instruction);

        AddLabel("COM port", 22, 102);
        portCombo.DropDownStyle = ComboBoxStyle.DropDownList;
        portCombo.Location = new Point(110, 98);
        portCombo.Size = new Size(180, 30);
        Controls.Add(portCombo);

        refreshButton.Text = "Refresh";
        refreshButton.Location = new Point(305, 96);
        refreshButton.Size = new Size(95, 32);
        refreshButton.Click += delegate { RefreshPorts(); };
        Controls.Add(refreshButton);

        AddLabel("JSON file", 22, 151);
        fileText.Location = new Point(110, 147);
        fileText.Size = new Size(475, 30);
        fileText.Text = GetDefaultJsonPath();
        fileText.Leave += delegate { LoadJsonPreview(fileText.Text); };
        Controls.Add(fileText);

        browseButton.Text = "Browse...";
        browseButton.Location = new Point(598, 145);
        browseButton.Size = new Size(98, 32);
        browseButton.Click += BrowseClicked;
        Controls.Add(browseButton);

        previewLabel.Location = new Point(110, 187);
        previewLabel.Size = new Size(585, 48);
        previewLabel.ForeColor = Color.DarkSlateGray;
        Controls.Add(previewLabel);

        uploadButton.Text = "Upload Profile";
        uploadButton.Font = new Font("Segoe UI", 11F, FontStyle.Bold);
        uploadButton.Location = new Point(22, 246);
        uploadButton.Size = new Size(180, 42);
        uploadButton.Click += delegate { UploadProfile(); };
        Controls.Add(uploadButton);

        statusLabel.Text = "Ready";
        statusLabel.Location = new Point(220, 254);
        statusLabel.Size = new Size(475, 25);
        Controls.Add(statusLabel);

        progressBar.Location = new Point(22, 303);
        progressBar.Size = new Size(674, 25);
        Controls.Add(progressBar);

        AddLabel("Log", 22, 347);
        logBox.Location = new Point(22, 372);
        logBox.Size = new Size(674, 140);
        logBox.Multiline = true;
        logBox.ReadOnly = true;
        logBox.ScrollBars = ScrollBars.Vertical;
        logBox.Font = new Font("Consolas", 9F);
        Controls.Add(logBox);

        FormClosing += delegate
        {
            if (serial != null && serial.IsOpen)
                serial.Close();
        };

        RefreshPorts();
        LoadJsonPreview(fileText.Text);
    }

    private void AddLabel(string text, int x, int y)
    {
        Label label = new Label();
        label.Text = text;
        label.Location = new Point(x, y);
        label.AutoSize = true;
        Controls.Add(label);
    }

    private static string GetDefaultJsonPath()
    {
        string toolsDirectory = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        DirectoryInfo parent = Directory.GetParent(toolsDirectory);
        if (parent == null)
            return "sensor_profiles.json";
        return Path.Combine(parent.FullName, "config", "sensor_profiles.json");
    }

    private void RefreshPorts()
    {
        string selected = portCombo.Text;
        string[] ports = SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
        portCombo.Items.Clear();
        portCombo.Items.AddRange(ports);
        int oldIndex = Array.IndexOf(ports, selected);
        if (oldIndex >= 0)
            portCombo.SelectedIndex = oldIndex;
        else if (ports.Length > 0)
            portCombo.SelectedIndex = ports.Length - 1;
        AddLog("Ports: " + (ports.Length == 0 ? "none" : string.Join(", ", ports)));
    }

    private void BrowseClicked(object sender, EventArgs e)
    {
        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*";
            dialog.FileName = fileText.Text;
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                fileText.Text = dialog.FileName;
                LoadJsonPreview(fileText.Text);
            }
        }
    }

    private byte[] ReadAndValidateJson(string path, bool updatePreview)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("JSON file not found.", path);

        byte[] bytes = File.ReadAllBytes(path);
        if (bytes.Length == 0 || bytes.Length > MaxJsonSize)
            throw new InvalidDataException("JSON size must be 1..65536 bytes.");

        string text = new UTF8Encoding(false, true).GetString(bytes);
        object decoded = new JavaScriptSerializer().DeserializeObject(text);
        IDictionary<string, object> root = decoded as IDictionary<string, object>;
        if (root == null || !root.ContainsKey("schema_version") ||
            !root.ContainsKey("config_version") || !root.ContainsKey("profiles"))
            throw new InvalidDataException("Missing schema_version, config_version, or profiles.");

        object[] profiles = root["profiles"] as object[];
        if (profiles == null)
            throw new InvalidDataException("profiles must be a JSON array.");

        List<string> productNumbers = new List<string>();
        foreach (object entry in profiles)
        {
            IDictionary<string, object> profile = entry as IDictionary<string, object>;
            if (profile != null && profile.ContainsKey("product_num"))
                productNumbers.Add(Convert.ToString(profile["product_num"]));
        }

        if (updatePreview)
        {
            previewLabel.Text = string.Format(
                "Version: {0}    Profiles: {1}\r\nPN: {2}    Size: {3} bytes",
                root["config_version"], profiles.Length,
                productNumbers.Count == 0 ? "none" : string.Join(", ", productNumbers.ToArray()),
                bytes.Length);
        }
        return bytes;
    }

    private byte[] LoadJsonPreview(string path)
    {
        try
        {
            return ReadAndValidateJson(path, true);
        }
        catch (Exception ex)
        {
            previewLabel.Text = "Invalid JSON: " + ex.Message;
            return null;
        }
    }

    private void UploadProfile()
    {
        string portName = portCombo.Text.Trim();
        if (portName.Length == 0)
        {
            MessageBox.Show(this, "Select a COM port first.", Text,
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        byte[] payload;
        try
        {
            payload = ReadAndValidateJson(fileText.Text.Trim(), true);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Invalid JSON",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        SetUiEnabled(false);
        progressBar.Value = 0;
        statusLabel.Text = "Opening " + portName + " ...";
        AddLog("Open " + portName + " with DTR/RTS disabled");

        try
        {
            uint crc = ComputeCrc32(payload);
            byte[] header = new byte[15];
            Array.Copy(Magic, 0, header, 0, Magic.Length);
            Array.Copy(BitConverter.GetBytes((uint)payload.Length), 0, header, 7, 4);
            Array.Copy(BitConverter.GetBytes(crc), 0, header, 11, 4);

            serial = new SerialPort();
            serial.PortName = portName;
            serial.BaudRate = 115200;
            serial.DataBits = 8;
            serial.Parity = Parity.None;
            serial.StopBits = StopBits.One;
            serial.Handshake = Handshake.None;
            serial.DtrEnable = false;
            serial.RtsEnable = false;
            serial.ReadTimeout = 200;
            serial.WriteTimeout = 10000;
            serial.NewLine = "\n";
            serial.Open();
            Thread.Sleep(100);
            serial.DiscardInBuffer();

            statusLabel.Text = "Send header ...";
            serial.Write(header, 0, header.Length);
            WaitControl('R', 3000);
            AddLog(string.Format("Header accepted, length={0}, CRC32=0x{1:X8}", payload.Length, crc));

            for (int offset = 0; offset < payload.Length; offset += ChunkSize)
            {
                int count = Math.Min(ChunkSize, payload.Length - offset);
                serial.Write(payload, offset, count);
                WaitControl('P', 3000);
                int sent = offset + count;
                progressBar.Value = Math.Min(100, (int)(100L * sent / payload.Length));
                statusLabel.Text = string.Format("Uploading: {0} / {1} bytes", sent, payload.Length);
                Application.DoEvents();
            }

            serial.ReadTimeout = 15000;
            string response = serial.ReadLine().Trim();
            AddLog("Device response: " + response);
            if (response != "ACK")
                throw new IOException("Upload failed: " + response);

            progressBar.Value = 100;
            statusLabel.Text = "Success - device is restarting";
            MessageBox.Show(this, "Profile installed successfully. The device is restarting.",
                Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            statusLabel.Text = "Failed";
            AddLog("ERROR: " + ex.Message);
            MessageBox.Show(this, ex.Message, "Upload failed",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            if (serial != null)
            {
                if (serial.IsOpen)
                    serial.Close();
                serial.Dispose();
                serial = null;
            }
            SetUiEnabled(true);
        }
    }

    private void WaitControl(int expected, int timeoutMs)
    {
        Stopwatch watch = Stopwatch.StartNew();
        while (watch.ElapsedMilliseconds < timeoutMs)
        {
            try
            {
                int value = serial.ReadByte();
                if (value == expected)
                    return;
                if (value == 'N')
                {
                    string remainder;
                    try { remainder = serial.ReadLine().Trim(); }
                    catch (TimeoutException) { remainder = "AK"; }
                    throw new IOException("Device rejected request: N" + remainder);
                }
            }
            catch (TimeoutException)
            {
                Application.DoEvents();
            }
        }
        throw new TimeoutException("Handshake timeout waiting for '" + (char)expected + "'.");
    }

    private static uint ComputeCrc32(byte[] data)
    {
        uint crc = 0xFFFFFFFFU;
        foreach (byte value in data)
        {
            crc ^= value;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc & 1U) != 0 ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
        return crc ^ 0xFFFFFFFFU;
    }

    private void SetUiEnabled(bool enabled)
    {
        portCombo.Enabled = enabled;
        refreshButton.Enabled = enabled;
        fileText.Enabled = enabled;
        browseButton.Enabled = enabled;
        uploadButton.Enabled = enabled;
    }

    private void AddLog(string text)
    {
        logBox.AppendText("[" + DateTime.Now.ToString("HH:mm:ss") + "] " + text + "\r\n");
        logBox.SelectionStart = logBox.TextLength;
        logBox.ScrollToCaret();
        Application.DoEvents();
    }
}
