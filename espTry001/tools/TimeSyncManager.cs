// Pyxis SensorLink TIME SYNC GUI - ESPTIME1
// Build with tools\Build_TimeSyncManager.bat.

using System;
using System.Drawing;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

namespace PyxisTimeSyncManager
{
    internal static class Strings
    {
        public static bool English;

        public static string T(string chinese, string english)
        {
            return English ? english : chinese;
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }

    internal sealed class EspTimeClient : IDisposable
    {
        private SerialPort _port;

        public bool Connected
        {
            get { return _port != null && _port.IsOpen; }
        }

        public string PortName
        {
            get { return Connected ? _port.PortName : ""; }
        }

        public string Connect(string portName)
        {
            DisposePort();
            _port = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One);
            _port.ReadTimeout = 1500;
            _port.WriteTimeout = 5000;
            _port.NewLine = "\n";
            _port.DtrEnable = false;
            _port.RtsEnable = false;
            _port.Open();
            Thread.Sleep(200);
            _port.DiscardInBuffer();

            Exception lastError = null;
            for (int attempt = 1; attempt <= 4; ++attempt)
            {
                try
                {
                    string pong = Command("PING", 1500);
                    if (!pong.StartsWith("PONG ESPTIME1 "))
                        throw new InvalidOperationException(Strings.T(
                            "握手响应异常: ", "Unexpected handshake response: ") + pong);
                    return pong;
                }
                catch (Exception error)
                {
                    lastError = error;
                    try { _port.DiscardInBuffer(); } catch { }
                    Thread.Sleep(350);
                }
            }

            DisposePort();
            throw new TimeoutException(
                Strings.T(
                    "ESPTIME1 握手失败。请确认设备停留在 TIME SYNC 页面，并选择了正确的 COM 口。",
                    "ESPTIME1 handshake failed. Keep the device on the TIME SYNC page and select the correct COM port."),
                lastError);
        }

        public string Command(string command)
        {
            return Command(command, 3000);
        }

        public string Command(string command, int timeoutMs)
        {
            if (!Connected) throw new InvalidOperationException(
                Strings.T("尚未连接设备", "Device is not connected"));
            _port.WriteLine(command);
            string response = ReadProtocolLine(timeoutMs);
            if (response.StartsWith("ERR "))
                throw new InvalidOperationException(Strings.T(
                    "设备拒绝请求: ", "Device rejected request: ") + response);
            return response;
        }

        public string ReadRtc()
        {
            if (!Connected) throw new InvalidOperationException(
                Strings.T("尚未连接设备", "Device is not connected"));
            _port.WriteLine("GET");
            string response = ReadProtocolLine(3000);
            return response;
        }

        private string ReadProtocolLine(int timeoutMs)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline)
            {
                int remaining = (int)(deadline - DateTime.UtcNow).TotalMilliseconds;
                _port.ReadTimeout = Math.Max(100, remaining);
                try
                {
                    string line = _port.ReadLine().TrimEnd('\r');
                    // ESP-IDF/Arduino diagnostics use forms such as
                    // [1499723][E][STA.cpp:540] ... . They are not ESPTIME1
                    // replies and must not shift the request/response stream.
                    if (line.StartsWith("[") && line.IndexOf("][", StringComparison.Ordinal) > 0)
                        continue;
                    return line;
                }
                catch (TimeoutException)
                {
                    if (DateTime.UtcNow >= deadline) throw;
                }
            }
            throw new TimeoutException(Strings.T(
                "等待设备协议响应超时", "Timed out waiting for device response"));
        }

        public string SyncComputerTime()
        {
            long utc = (long)(DateTime.UtcNow -
                new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).TotalSeconds;
            string result = Command("SET " + utc.ToString(), 5000);
            if (!result.StartsWith("OK "))
                throw new InvalidOperationException(Strings.T(
                    "设置 RTC 响应异常: ", "Unexpected RTC response: ") + result);
            return result;
        }

        public string ReadWifiStatus()
        {
            string result = Command("WIFI STATUS");
            if (!result.StartsWith("WIFI "))
                throw new InvalidOperationException(Strings.T(
                    "Wi-Fi 状态响应异常: ", "Unexpected Wi-Fi status response: ") + result);
            return result;
        }

        public bool TryReadCredentials(out string ssid, out string password)
        {
            ssid = "";
            password = "";
            try
            {
                string result = Command("WIFI GET");
                string[] parts = result.Split(' ');
                if (parts.Length != 4 || parts[0] != "WIFI" ||
                    parts[1] != "CREDENTIALS")
                    throw new InvalidOperationException(Strings.T(
                        "Wi-Fi 凭据响应异常: ", "Unexpected Wi-Fi credentials response: ") + result);
                ssid = DecodeHex(parts[2], false);
                password = DecodeHex(parts[3], true);
                return true;
            }
            catch (InvalidOperationException error)
            {
                if (error.Message.IndexOf("NO WIFI CONFIG", StringComparison.Ordinal) >= 0 ||
                    error.Message.IndexOf("UNKNOWN COMMAND", StringComparison.Ordinal) >= 0)
                    return false;
                throw;
            }
        }

        public string SaveWifiAndStartNtp(string ssid, string password)
        {
            string ssidToken = EncodeHex(ssid, 32, false,
                Strings.T("Wi-Fi 名称", "Wi-Fi name"));
            string passwordToken = EncodeHex(password, 63, true,
                Strings.T("Wi-Fi 密码", "Wi-Fi password"));
            string stored = Command("WIFI SET " + ssidToken + " " + passwordToken, 5000);
            if (!stored.StartsWith("OK WIFI STORED"))
                throw new InvalidOperationException(Strings.T(
                    "保存 Wi-Fi 响应异常: ", "Unexpected Wi-Fi save response: ") + stored);
            string started = Command("NTP START", 5000);
            if (!started.StartsWith("OK NTP STARTED"))
                throw new InvalidOperationException(Strings.T(
                    "启动 NTP 响应异常: ", "Unexpected NTP start response: ") + started);
            return stored + "; " + started;
        }

        public string StartNtp()
        {
            string result = Command("NTP START", 5000);
            if (!result.StartsWith("OK NTP STARTED"))
                throw new InvalidOperationException(Strings.T(
                    "启动 NTP 响应异常: ", "Unexpected NTP start response: ") + result);
            return result;
        }

        public string ClearWifi()
        {
            string result = Command("WIFI CLEAR", 5000);
            if (!result.StartsWith("OK WIFI CLEARED"))
                throw new InvalidOperationException(Strings.T(
                    "清除 Wi-Fi 响应异常: ", "Unexpected Wi-Fi clear response: ") + result);
            return result;
        }

        public void CloseSession()
        {
            if (!Connected) return;
            try { Command("CLOSE", 2000); } catch { }
            DisposePort();
        }

        private static string EncodeHex(string value, int maximumBytes,
                                        bool allowEmpty, string label)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
            if (bytes.Length == 0 && !allowEmpty)
                throw new ArgumentException(label + Strings.T("不能为空", " cannot be empty"));
            if (bytes.Length > maximumBytes)
                throw new ArgumentException(label + Strings.T(
                    "超过 " + maximumBytes + " 个 UTF-8 字节",
                    " exceeds " + maximumBytes + " UTF-8 bytes"));
            if (bytes.Length == 0) return "-";
            var builder = new StringBuilder(bytes.Length * 2);
            foreach (byte valueByte in bytes) builder.Append(valueByte.ToString("x2"));
            return builder.ToString();
        }

        private static string DecodeHex(string token, bool dashIsEmpty)
        {
            if (dashIsEmpty && token == "-") return "";
            if (String.IsNullOrEmpty(token) || token.Length % 2 != 0)
                throw new FormatException(Strings.T(
                    "设备返回了无效的十六进制字段",
                    "Device returned an invalid hexadecimal field"));
            byte[] bytes = new byte[token.Length / 2];
            for (int i = 0; i < bytes.Length; ++i)
                bytes[i] = Convert.ToByte(token.Substring(i * 2, 2), 16);
            return Encoding.UTF8.GetString(bytes);
        }

        private void DisposePort()
        {
            if (_port == null) return;
            try { if (_port.IsOpen) _port.Close(); } catch { }
            _port.Dispose();
            _port = null;
        }

        public void Dispose()
        {
            DisposePort();
        }
    }

    internal sealed class DeviceSnapshot
    {
        public string Rtc = "";
        public string WifiStatus = "";
        public bool HasCredentials;
        public string Ssid = "";
        public string Password = "";
    }

    internal sealed class MainForm : Form
    {
        private const string RegistryPath = @"Software\Pyxis\SensorLink\TimeSyncManager";
        private readonly ComboBox _ports = new ComboBox();
        private readonly ComboBox _language = new ComboBox();
        private readonly Label _portCaption = new Label();
        private readonly Label _languageCaption = new Label();
        private readonly Button _refreshPorts = new Button();
        private readonly Button _connect = new Button();
        private readonly Button _disconnect = new Button();
        private readonly Label _connection = new Label();
        private readonly Label _computerTime = new Label();
        private readonly Label _deviceTime = new Label();
        private readonly Label _computerTimeCaption = new Label();
        private readonly Label _deviceTimeCaption = new Label();
        private readonly GroupBox _rtcGroup = new GroupBox();
        private readonly Button _syncRtc = new Button();
        private readonly TextBox _ssid = new TextBox();
        private readonly TextBox _password = new TextBox();
        private readonly CheckBox _showPassword = new CheckBox();
        private readonly Label _ssidCaption = new Label();
        private readonly Label _passwordCaption = new Label();
        private readonly Label _deviceStatusCaption = new Label();
        private readonly Label _ntpInfo = new Label();
        private readonly Label _logCaption = new Label();
        private readonly GroupBox _wifiGroup = new GroupBox();
        private readonly Button _loadWifi = new Button();
        private readonly Button _saveWifi = new Button();
        private readonly Button _retryNtp = new Button();
        private readonly Button _clearWifi = new Button();
        private readonly Label _wifiStatus = new Label();
        private readonly TextBox _log = new TextBox();
        private readonly System.Windows.Forms.Timer _timer =
            new System.Windows.Forms.Timer();
        private EspTimeClient _client;
        private bool _busy;
        private bool _loadingLanguage;
        private int _statusTick;

        public MainForm()
        {
            LoadLanguagePreference();
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(760, 610);
            Size = new Size(820, 660);
            Font = new Font("Microsoft YaHei UI", 9F);

            BuildInterface();
            ApplyLanguage();
            RefreshPortList();
            UpdateConnectionUi();

            _timer.Interval = 1000;
            _timer.Tick += async delegate
            {
                _computerTime.Text = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") +
                    Strings.T("（电脑本地时间）", " (computer local time)");
                _statusTick++;
                if (_statusTick % 2 == 0 && IsConnected && !_busy)
                    await RefreshSnapshotAsync(false, false);
            };
            _timer.Start();
            FormClosing += delegate { if (_client != null) _client.Dispose(); };
        }

        private void LoadLanguagePreference()
        {
            string saved = null;
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RegistryPath))
                {
                    if (key != null) saved = key.GetValue("Language") as string;
                }
            }
            catch { }
            if (saved == "zh-CN") Strings.English = false;
            else if (saved == "en") Strings.English = true;
            else Strings.English = !CultureInfo.CurrentUICulture.Name.StartsWith(
                "zh", StringComparison.OrdinalIgnoreCase);
        }

        private void SaveLanguagePreference()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RegistryPath))
                {
                    if (key != null)
                        key.SetValue("Language", Strings.English ? "en" : "zh-CN");
                }
            }
            catch { }
        }

        private bool IsConnected
        {
            get { return _client != null && _client.Connected; }
        }

        private void BuildInterface()
        {
            var root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.Padding = new Padding(10);
            root.ColumnCount = 1;
            root.RowCount = 5;
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 110));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 215));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(root);

            var serialBar = new FlowLayoutPanel();
            serialBar.Dock = DockStyle.Fill;
            serialBar.WrapContents = false;
            _portCaption.AutoSize = true;
            _portCaption.Margin = new Padding(3, 10, 3, 3);
            serialBar.Controls.Add(_portCaption);
            _ports.DropDownStyle = ComboBoxStyle.DropDownList;
            _ports.Width = 115;
            _ports.Margin = new Padding(3, 6, 3, 3);
            serialBar.Controls.Add(_ports);
            ConfigureButton(_refreshPorts, "", 65);
            ConfigureButton(_connect, "", 85);
            ConfigureButton(_disconnect, "", 100);
            serialBar.Controls.Add(_refreshPorts);
            serialBar.Controls.Add(_connect);
            serialBar.Controls.Add(_disconnect);
            _connection.AutoSize = true;
            _connection.Margin = new Padding(12, 10, 3, 3);
            serialBar.Controls.Add(_connection);
            _languageCaption.AutoSize = true;
            _languageCaption.Margin = new Padding(12, 10, 3, 3);
            serialBar.Controls.Add(_languageCaption);
            _language.DropDownStyle = ComboBoxStyle.DropDownList;
            _language.Width = 100;
            _language.Margin = new Padding(3, 6, 3, 3);
            _language.Items.Add("简体中文");
            _language.Items.Add("English");
            _loadingLanguage = true;
            _language.SelectedIndex = Strings.English ? 1 : 0;
            _loadingLanguage = false;
            serialBar.Controls.Add(_language);
            root.Controls.Add(serialBar, 0, 0);

            _rtcGroup.Dock = DockStyle.Fill;
            var rtcTable = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 2 };
            rtcTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
            rtcTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            rtcTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 175));
            _computerTimeCaption.AutoSize = true;
            _computerTimeCaption.Anchor = AnchorStyles.Left;
            rtcTable.Controls.Add(_computerTimeCaption, 0, 0);
            _computerTime.AutoSize = true;
            _computerTime.Anchor = AnchorStyles.Left;
            rtcTable.Controls.Add(_computerTime, 1, 0);
            _deviceTimeCaption.AutoSize = true;
            _deviceTimeCaption.Anchor = AnchorStyles.Left;
            rtcTable.Controls.Add(_deviceTimeCaption, 0, 1);
            _deviceTime.AutoSize = true;
            _deviceTime.Anchor = AnchorStyles.Left;
            rtcTable.Controls.Add(_deviceTime, 1, 1);
            ConfigureButton(_syncRtc, "", 160);
            _syncRtc.Dock = DockStyle.Fill;
            rtcTable.Controls.Add(_syncRtc, 2, 0);
            rtcTable.SetRowSpan(_syncRtc, 2);
            _rtcGroup.Controls.Add(rtcTable);
            root.Controls.Add(_rtcGroup, 0, 1);

            _wifiGroup.Dock = DockStyle.Fill;
            var wifiTable = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 5, Padding = new Padding(5) };
            wifiTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 95));
            wifiTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            wifiTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
            wifiTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
            _ssidCaption.AutoSize = true;
            _ssidCaption.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_ssidCaption, 0, 0);
            _ssid.Dock = DockStyle.Fill;
            _ssid.MaxLength = 32;
            wifiTable.Controls.Add(_ssid, 1, 0);
            wifiTable.SetColumnSpan(_ssid, 3);
            _passwordCaption.AutoSize = true;
            _passwordCaption.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_passwordCaption, 0, 1);
            _password.Dock = DockStyle.Fill;
            _password.MaxLength = 63;
            _password.UseSystemPasswordChar = true;
            wifiTable.Controls.Add(_password, 1, 1);
            wifiTable.SetColumnSpan(_password, 2);
            _showPassword.AutoSize = true;
            _showPassword.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_showPassword, 3, 1);
            _deviceStatusCaption.AutoSize = true;
            _deviceStatusCaption.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_deviceStatusCaption, 0, 2);
            _wifiStatus.AutoSize = true;
            _wifiStatus.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_wifiStatus, 1, 2);
            wifiTable.SetColumnSpan(_wifiStatus, 3);
            _ntpInfo.AutoSize = true;
            _ntpInfo.ForeColor = Color.DimGray;
            _ntpInfo.Anchor = AnchorStyles.Left;
            wifiTable.Controls.Add(_ntpInfo, 0, 3);
            wifiTable.SetColumnSpan(_ntpInfo, 4);
            var wifiButtons = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
            ConfigureButton(_loadWifi, "", 110);
            ConfigureButton(_saveWifi, "", 125);
            ConfigureButton(_retryNtp, "", 115);
            ConfigureButton(_clearWifi, "", 100);
            wifiButtons.Controls.Add(_loadWifi);
            wifiButtons.Controls.Add(_saveWifi);
            wifiButtons.Controls.Add(_retryNtp);
            wifiButtons.Controls.Add(_clearWifi);
            wifiTable.Controls.Add(wifiButtons, 0, 4);
            wifiTable.SetColumnSpan(wifiButtons, 4);
            _wifiGroup.Controls.Add(wifiTable);
            root.Controls.Add(_wifiGroup, 0, 2);

            _logCaption.Dock = DockStyle.Fill;
            _logCaption.ForeColor = Color.DimGray;
            root.Controls.Add(_logCaption, 0, 3);
            _log.Dock = DockStyle.Fill;
            _log.Multiline = true;
            _log.ReadOnly = true;
            _log.ScrollBars = ScrollBars.Vertical;
            _log.BackColor = Color.White;
            root.Controls.Add(_log, 0, 4);

            _refreshPorts.Click += delegate { RefreshPortList(); };
            _connect.Click += async delegate { await ConnectAsync(); };
            _disconnect.Click += async delegate { await DisconnectAsync(); };
            _syncRtc.Click += async delegate { await SyncRtcAsync(); };
            _loadWifi.Click += async delegate { await RefreshSnapshotAsync(true, true); };
            _saveWifi.Click += async delegate { await SaveWifiAsync(); };
            _retryNtp.Click += async delegate { await RetryNtpAsync(); };
            _clearWifi.Click += async delegate { await ClearWifiAsync(); };
            _showPassword.CheckedChanged += delegate
            {
                _password.UseSystemPasswordChar = !_showPassword.Checked;
            };
            _language.SelectedIndexChanged += delegate
            {
                if (_loadingLanguage) return;
                Strings.English = _language.SelectedIndex == 1;
                SaveLanguagePreference();
                ApplyLanguage();
            };
        }

        private static void ConfigureButton(Button button, string text, int width)
        {
            button.Text = text;
            button.Width = width;
            button.Height = 30;
            button.Margin = new Padding(3, 4, 3, 3);
        }

        private void ApplyLanguage()
        {
            Text = Strings.T("Pyxis SensorLink 时间同步",
                             "Pyxis SensorLink Time Sync");
            _portCaption.Text = Strings.T("设备串口：", "Device port:");
            _languageCaption.Text = Strings.T("语言：", "Language:");
            _refreshPorts.Text = Strings.T("刷新", "Refresh");
            _connect.Text = Strings.T("连接设备", "Connect");
            _disconnect.Text = Strings.T("断开并退出", "Disconnect");
            _rtcGroup.Text = Strings.T("RTC 时间", "RTC Time");
            _computerTimeCaption.Text = Strings.T("电脑当前时间：", "Computer time:");
            _deviceTimeCaption.Text = Strings.T("设备 RTC：", "Device RTC:");
            _syncRtc.Text = Strings.T("同步电脑时间到 RTC", "Sync computer time to RTC");
            _wifiGroup.Text = Strings.T(
                "手机热点 / Wi-Fi（保存后设备会自动重试 NTP）",
                "Mobile hotspot / Wi-Fi (device retries NTP automatically)");
            _ssidCaption.Text = Strings.T("Wi-Fi 名称：", "Wi-Fi name:");
            _passwordCaption.Text = Strings.T("Wi-Fi 密码：", "Wi-Fi password:");
            _showPassword.Text = Strings.T("显示密码", "Show password");
            _deviceStatusCaption.Text = Strings.T("设备状态：", "Device status:");
            _ntpInfo.Text = Strings.T(
                "NTP 自动回退：pool.ntp.org / time.cloudflare.com / ntp.tencent.com",
                "NTP fallback: pool.ntp.org / time.cloudflare.com / ntp.tencent.com");
            _loadWifi.Text = Strings.T("读取设备配置", "Read device config");
            _saveWifi.Text = Strings.T("保存并立即校时", "Save and sync now");
            _retryNtp.Text = Strings.T("立即重试 NTP", "Retry NTP now");
            _clearWifi.Text = Strings.T("清除 Wi-Fi", "Clear Wi-Fi");
            _logCaption.Text = Strings.T(
                "操作日志（设备必须停留在 TIME SYNC 页面）",
                "Operation log (device must remain on the TIME SYNC page)");
            _computerTime.Text = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") +
                Strings.T("（电脑本地时间）", " (computer local time)");
            UpdateConnectionUi();
        }

        private void RefreshPortList()
        {
            string selected = _ports.SelectedItem as string;
            string[] names = SerialPort.GetPortNames();
            Array.Sort(names, StringComparer.OrdinalIgnoreCase);
            _ports.Items.Clear();
            _ports.Items.AddRange(names);
            if (!String.IsNullOrEmpty(selected) && _ports.Items.Contains(selected))
                _ports.SelectedItem = selected;
            else if (_ports.Items.Count > 0)
                _ports.SelectedIndex = 0;
            AppendLog(Strings.T("已刷新串口，共 ", "Serial ports refreshed: ") +
                names.Length + Strings.T(" 个。", " found."));
        }

        private async Task ConnectAsync()
        {
            if (_ports.SelectedItem == null)
            {
                MessageBox.Show(this, Strings.T(
                    "没有可用串口，请连接设备后刷新。",
                    "No serial port is available. Connect the device and refresh."),
                    Strings.T("时间同步", "Time Sync"),
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string portName = _ports.SelectedItem.ToString();
            await RunAsync(Strings.T("连接 ", "Connect ") + portName, delegate
            {
                if (_client != null) _client.Dispose();
                _client = new EspTimeClient();
                return _client.Connect(portName);
            });
            if (IsConnected) await RefreshSnapshotAsync(true, false);
        }

        private async Task DisconnectAsync()
        {
            if (!IsConnected) return;
            await RunAsync(Strings.T("断开设备", "Disconnect device"), delegate
            {
                _client.CloseSession();
                return Strings.T("设备已退出 TIME SYNC 会话",
                                 "Device exited the TIME SYNC session");
            });
        }

        private async Task SyncRtcAsync()
        {
            if (!RequireConnection()) return;
            await RunAsync(Strings.T("同步 RTC", "Sync RTC"),
                delegate { return _client.SyncComputerTime(); });
            if (IsConnected) await RefreshSnapshotAsync(false, false);
        }

        private async Task SaveWifiAsync()
        {
            if (!RequireConnection()) return;
            string ssid = _ssid.Text;
            string password = _password.Text;
            await RunAsync(Strings.T("保存 Wi-Fi 并启动 NTP",
                                     "Save Wi-Fi and start NTP"), delegate
            {
                return _client.SaveWifiAndStartNtp(ssid, password);
            });
            if (IsConnected) await RefreshSnapshotAsync(false, false);
        }

        private async Task RetryNtpAsync()
        {
            if (!RequireConnection()) return;
            await RunAsync(Strings.T("立即重试 NTP", "Retry NTP now"),
                delegate { return _client.StartNtp(); });
            if (IsConnected) await RefreshSnapshotAsync(false, false);
        }

        private async Task ClearWifiAsync()
        {
            if (!RequireConnection()) return;
            DialogResult answer = MessageBox.Show(this, Strings.T(
                "确定清除设备中保存的 Wi-Fi 名称和密码吗？",
                "Clear the Wi-Fi name and password stored on the device?"),
                Strings.T("清除 Wi-Fi", "Clear Wi-Fi"),
                MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
            if (answer != DialogResult.Yes) return;
            await RunAsync(Strings.T("清除 Wi-Fi", "Clear Wi-Fi"),
                delegate { return _client.ClearWifi(); });
            if (IsConnected)
            {
                _ssid.Text = "";
                _password.Text = "";
                await RefreshSnapshotAsync(false, false);
            }
        }

        private async Task RefreshSnapshotAsync(bool loadCredentials,
                                                bool reportResult)
        {
            if (!IsConnected || _busy) return;
            _busy = true;
            UpdateConnectionUi();
            try
            {
                DeviceSnapshot snapshot = await Task.Run(delegate
                {
                    var value = new DeviceSnapshot();
                    value.Rtc = _client.ReadRtc();
                    value.WifiStatus = _client.ReadWifiStatus();
                    if (loadCredentials)
                    {
                        string ssid;
                        string password;
                        value.HasCredentials = _client.TryReadCredentials(
                            out ssid, out password);
                        value.Ssid = ssid;
                        value.Password = password;
                    }
                    return value;
                });
                _deviceTime.Text = snapshot.Rtc;
                _wifiStatus.Text = snapshot.WifiStatus;
                if (loadCredentials)
                {
                    _ssid.Text = snapshot.Ssid;
                    _password.Text = snapshot.Password;
                }
                if (reportResult) AppendLog(Strings.T(
                    "已读取设备时间和 Wi-Fi 配置。",
                    "Device time and Wi-Fi configuration loaded."));
            }
            catch (Exception error)
            {
                AppendLog(Strings.T("刷新失败：", "Refresh failed: ") +
                    FriendlyError(error));
            }
            finally
            {
                _busy = false;
                UpdateConnectionUi();
            }
        }

        private async Task RunAsync(string action, Func<string> work)
        {
            if (_busy) return;
            _busy = true;
            UpdateConnectionUi();
            AppendLog(action + "...");
            try
            {
                string result = await Task.Run(work);
                AppendLog(Strings.T("成功：", "Success: ") + result);
            }
            catch (Exception error)
            {
                AppendLog(Strings.T("失败：", "Failed: ") + FriendlyError(error));
                MessageBox.Show(this, FriendlyError(error), action,
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                _busy = false;
                UpdateConnectionUi();
            }
        }

        private bool RequireConnection()
        {
            if (IsConnected) return true;
            MessageBox.Show(this, Strings.T(
                "请先选择串口并连接设备。",
                "Select a serial port and connect the device first."),
                Strings.T("时间同步", "Time Sync"),
                MessageBoxButtons.OK, MessageBoxIcon.Information);
            return false;
        }

        private void UpdateConnectionUi()
        {
            bool connected = IsConnected;
            _connection.Text = connected
                ? Strings.T("已连接 ", "Connected ") + _client.PortName
                : Strings.T("未连接", "Not connected");
            _connection.ForeColor = connected ? Color.Green : Color.Firebrick;
            _ports.Enabled = !_busy && !connected;
            _refreshPorts.Enabled = !_busy && !connected;
            _connect.Enabled = !_busy && !connected;
            _disconnect.Enabled = !_busy && connected;
            _syncRtc.Enabled = !_busy && connected;
            _loadWifi.Enabled = !_busy && connected;
            _saveWifi.Enabled = !_busy && connected;
            _retryNtp.Enabled = !_busy && connected;
            _clearWifi.Enabled = !_busy && connected;
        }

        private void AppendLog(string message)
        {
            _log.AppendText("[" + DateTime.Now.ToString("HH:mm:ss") + "] " +
                message + Environment.NewLine);
        }

        private static string FriendlyError(Exception error)
        {
            if (error is TimeoutException) return error.Message;
            if (error is UnauthorizedAccessException)
                return Strings.T(
                    "串口被其他程序占用，或当前用户没有访问权限。",
                    "The serial port is in use by another program or access was denied.");
            return error.Message;
        }
    }
}
