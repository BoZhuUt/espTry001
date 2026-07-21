// SensorDataExporter - ESPLOG1 SD 日志导出上位机
//
// 单文件 WinForms 程序，与 SensorProfileUpdater.cs 风格一致。
// 编译：tools\Build_SensorDataExporter.bat
//
// 功能：列出设备 /logs/*.csv，多选下载并校验 CRC32，
//       支持断点续传（.part 文件），二次确认删除。

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace PyxisSensorDataExporter
{
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

    // =========================================================================
    // ESPLOG1 协议封装
    // =========================================================================

    internal sealed class LogFileEntry
    {
        public string Path;
        public long Size;
        public long Session;
        public long Pn;
        public string FileName { get { return System.IO.Path.GetFileName(Path); } }
    }

    internal sealed class DownloadProgress
    {
        public string FileName;
        public long Offset;
        public long Total;
        public bool Done;
        public string Message;
    }

    internal sealed class EspLogClient : IDisposable
    {
        public const string Magic = "ESPLOG1";
        private const int BlockSize = 4096;

        private SerialPort _port;
        private byte[] _readBuf = new byte[1];

        public void Connect(string portName)
        {
            if (_port != null && _port.IsOpen)
                throw new InvalidOperationException("已经连接，请先断开");

            // DTR/RTS 保持 false，避免 ESP32-S3 原生 USB CDC 误触发复位。
            _port = new SerialPort();
            _port.PortName = portName;
            _port.BaudRate = 115200;
            _port.DataBits = 8;
            _port.Parity = Parity.None;
            _port.StopBits = StopBits.One;
            _port.ReadTimeout = 5000;
            _port.WriteTimeout = 10000;
            _port.ReadBufferSize = 65536;
            _port.DtrEnable = false;
            _port.RtsEnable = false;
            _port.Open();
            Thread.Sleep(100);
            _port.DiscardInBuffer();
        }

        public void Disconnect()
        {
            if (_port != null)
            {
                try { if (_port.IsOpen) _port.Close(); }
                catch { /* 忽略关闭异常 */ }
            }
        }

        public void Dispose() { Disconnect(); }

        // ---- 协议命令 ----
        // 每个高层方法开头清空输入缓冲，防止上次命令的残留响应污染本次解析。
        // 这种错位一旦发生会沿命令链向后传播，例如下载结束后设备主动发的
        // "OK Done" 没被消费，会被下一个 STAT 的 ReadLine 读到导致解析失败。

        public string[] Ping()
        {
            _port.DiscardInBuffer();
            SendLine("PING");
            // PONG ESPLOG1 <cfg> <fw>
            string line = ReadLine();
            string[] parts = line.Split(' ');
            if (parts.Length < 4 || parts[0] != "PONG" || parts[1] != Magic)
                throw new Exception("PING 响应异常: " + line);
            return new[] { parts[2], parts[3] }; // config, firmware
        }

        public List<LogFileEntry> ListFiles()
        {
            _port.DiscardInBuffer();
            SendLine("LIST");
            string countLine = ReadLine();
            if (!countLine.StartsWith("COUNT "))
                throw new Exception("LIST 响应异常: " + countLine);
            int count = int.Parse(countLine.Substring(6).Trim());

            var entries = new List<LogFileEntry>(count);
            for (int i = 0; i < count; i++)
            {
                string line = ReadLine();
                string[] f = line.Split('\t');
                if (f.Length < 4)
                    throw new Exception("LIST 条目格式错: " + line);
                entries.Add(new LogFileEntry
                {
                    Path = f[0],
                    Size = long.Parse(f[1]),
                    Session = long.Parse(f[2]),
                    Pn = long.Parse(f[3])
                });
            }
            string end = ReadLine();
            if (end != "END")
                throw new Exception("LIST 未以 END 结束: " + end);
            return entries;
        }

        /// <summary>查询文件大小和 CRC32（返回 size, crc32）</summary>
        public long[] Stat(string path)
        {
            _port.DiscardInBuffer();
            SendLine("STAT " + path);
            _port.ReadTimeout = 30000; // 大文件 CRC32 计算较慢
            try
            {
                string line = ReadLine();
                if (line.StartsWith("OK "))
                {
                    string[] p = line.Split(' ');
                    return new[] { long.Parse(p[1]), long.Parse(p[2], System.Globalization.NumberStyles.HexNumber) };
                }
                throw new Exception("STAT 失败: " + line);
            }
            finally { _port.ReadTimeout = 5000; }
        }

        /// <summary>下载文件到本地，支持断点续传（.part）。</summary>
        public void DownloadFile(string remotePath, string localPath,
                                 IProgress<DownloadProgress> progress,
                                 CancellationToken ct)
        {
            long[] stat = Stat(remotePath);
            long size = stat[0];
            long expectedCrc = stat[1];

            string partPath = localPath + ".part";
            long offset = 0;
            var partFile = new FileInfo(partPath);
            if (partFile.Exists && partFile.Length <= size)
            {
                offset = partFile.Length;
            }

            using (var fs = new FileStream(partPath, FileMode.Append, FileAccess.Write))
            {
                int resendCount = 0;
                while (offset < size)
                {
                    ct.ThrowIfCancellationRequested();
                    int length = (int)Math.Min(BlockSize, size - offset);
                    var block = GetBlock(remotePath, offset, length, ref resendCount);
                    if (block == null) // EOF（offset>=size 时的防御）
                        break;
                    fs.Write(block, 0, block.Length);
                    offset += block.Length;

                    if (progress != null)
                    {
                        progress.Report(new DownloadProgress
                        {
                            FileName = System.IO.Path.GetFileName(remotePath),
                            Offset = offset,
                            Total = size,
                            Done = false
                        });
                    }
                }

                // 最后一块 ACK 后设备会主动发 "OK Done" 作为传输完成确认。
                // 必须消费这一行，否则它会残留在串口缓冲，污染下一个命令（如
                // STAT 响应被读成 "OK Done"）。即使因为 part 文件已完整跳过
                // 循环，也必须尝试读取一次：只有设备真的没发这行时才会超时。
                if (size > 0 && offset >= size)
                {
                    try
                    {
                        string done = ReadLine();
                        if (done != "OK Done")
                        {
                            // 设备没按预期发 OK Done，可能是协议不同步。
                            // 抛异常让上层决定是否继续下一个文件。
                            throw new Exception("传输结束响应异常: " + done);
                        }
                    }
                    catch (TimeoutException)
                    {
                        // 断点续传已完整时 size>0 且 offset>=size 但设备不会再发数据，
                        // 这里允许超时通过。其它情况会被上面的异常捕获。
                    }
                }
            }

            // 整文件 CRC32 校验
            uint actualCrc = Crc32OfFile(partPath);
            if (actualCrc != expectedCrc)
            {
                File.Delete(partPath);
                throw new Exception(string.Format(
                    "CRC32 不匹配: 期望 {0:X8}, 实际 {1:X8}", expectedCrc, actualCrc));
            }

            // 校验通过，重命名为正式文件
            if (File.Exists(localPath)) File.Delete(localPath);
            File.Move(partPath, localPath);

            if (progress != null)
            {
                progress.Report(new DownloadProgress
                {
                    FileName = System.IO.Path.GetFileName(remotePath),
                    Offset = size,
                    Total = size,
                    Done = true,
                    Message = "CRC32 校验通过"
                });
            }
        }

        /// <summary>删除文件（两步确认）。</summary>
        public void Delete(string path)
        {
            _port.DiscardInBuffer();
            SendLine("DELETE " + path);
            string line = ReadLine();
            if (!line.StartsWith("CONFIRM "))
                throw new Exception("DELETE 失败: " + line);
            string token = line.Substring(8).Trim();
            SendLine("CONFIRM " + token);
            string result = ReadLine();
            if (!result.StartsWith("OK"))
                throw new Exception("DELETE 确认失败: " + result);
        }

        public void CloseSession()
        {
            try
            {
                _port.DiscardInBuffer();
                SendLine("CLOSE");
                ReadLine(); // BYE
            }
            catch { /* 退出时忽略通信异常 */ }
        }

        // ---- 内部 ----

        private byte[] GetBlock(string path, long offset, int length, ref int resendCount)
        {
            SendLine(string.Format("GET {0} {1} {2}", path, offset, length));
            string first = ReadLine();
            if (first == "EOF") return null;
            if (first.StartsWith("ERR"))
                throw new Exception("GET 失败: " + first);
            if (!first.StartsWith("DATA "))
                throw new Exception("GET 响应异常: " + first);

            int expectedLen = int.Parse(first.Substring(5).Trim());
            byte[] data = ReadBinary(expectedLen);

            string crcLine = ReadLine();
            if (!crcLine.StartsWith("CRC "))
                throw new Exception("GET CRC 行异常: " + crcLine);
            uint blockCrc = uint.Parse(crcLine.Substring(4).Trim(),
                                       System.Globalization.NumberStyles.HexNumber);

            uint actualCrc = Crc32.Compute(data, 0, data.Length);
            if (actualCrc != blockCrc)
            {
                if (resendCount >= 3)
                    throw new Exception("块 CRC 重试超限");
                resendCount++;
                SendLine("RESEND");
                return GetBlock(path, offset, length, ref resendCount);
            }
            resendCount = 0;
            SendLine("ACK");
            return data;
        }

        private void SendLine(string line)
        {
            byte[] data = Encoding.ASCII.GetBytes(line + "\n");
            _port.Write(data, 0, data.Length);
            _port.BaseStream.Flush();
        }

        private string ReadLine()
        {
            var sb = new StringBuilder();
            var deadline = DateTime.UtcNow.AddMilliseconds(_port.ReadTimeout);
            while (DateTime.UtcNow < deadline)
            {
                int b = ReadByte();
                if (b < 0) continue;
                if (b == '\n')
                {
                    if (sb.Length > 0 && sb[sb.Length - 1] == '\r')
                        sb.Length--;
                    return sb.ToString();
                }
                sb.Append((char)b);
            }
            throw new TimeoutException("读行超时，已收 " + sb.Length + " 字节");
        }

        private byte[] ReadBinary(int count)
        {
            byte[] buf = new byte[count];
            int read = 0;
            int oldTimeout = _port.ReadTimeout;
            _port.ReadTimeout = 5000;
            var deadline = DateTime.UtcNow.AddSeconds(60); // 总超时 60 秒
            try
            {
                while (read < count)
                {
                    if (DateTime.UtcNow >= deadline)
                        throw new IOException("数据块读取超时: " + read + "/" + count);
                    try
                    {
                        int n = _port.Read(buf, read, count - read);
                        if (n > 0) read += n;
                    }
                    catch (TimeoutException)
                    {
                        // 单次 5 秒超时，设备可能 SD 读取慢，继续等
                    }
                }
            }
            finally { _port.ReadTimeout = oldTimeout; }
            return buf;
        }

        private int ReadByte()
        {
            if (_port.BytesToRead == 0)
            {
                // 等待第一个字节，用短超时轮询
                int waited = 0;
                while (_port.BytesToRead == 0 && waited < _port.ReadTimeout)
                {
                    Thread.Sleep(5);
                    waited += 5;
                }
                if (_port.BytesToRead == 0) return -1;
            }
            int b = _port.ReadByte();
            return b;
        }

        private static uint Crc32OfFile(string path)
        {
            uint crc = 0xFFFFFFFF;
            byte[] buf = new byte[8192];
            using (var fs = File.OpenRead(path))
            {
                int n;
                while ((n = fs.Read(buf, 0, buf.Length)) > 0)
                    crc = Crc32.Update(crc, buf, n);
            }
            return crc ^ 0xFFFFFFFF;
        }
    }

    // =========================================================================
    // CRC32（与设备端一致，IEEE 802.3 多项式 0xEDB88320）
    // =========================================================================

    internal static class Crc32
    {
        public static uint Update(uint crc, byte[] data, int length)
        {
            for (int i = 0; i < length; i++)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            }
            return crc;
        }

        public static uint Compute(byte[] data, int offset, int length)
        {
            uint crc = 0xFFFFFFFF;
            for (int i = 0; i < length; i++)
            {
                crc ^= data[offset + i];
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            }
            return crc ^ 0xFFFFFFFF;
        }
    }

    // =========================================================================
    // 主窗口
    // =========================================================================

    internal sealed class MainForm : Form
    {
        private ComboBox _portCombo;
        private Button _refreshBtn, _connectBtn;
        private Label _statusLabel;
        private DataGridView _grid;
        private Button _listBtn, _selectAllBtn, _selectNoneBtn;
        private TextBox _dirBox;
        private Button _browseBtn, _downloadBtn, _openFolderBtn, _deleteBtn;
        private Label _currentFileLabel;
        private ProgressBar _progressBar;
        private Label _progressLabel;
        private TextBox _logBox;
        private Button _cancelBtn;

        private EspLogClient _client;
        private CancellationTokenSource _cts;
        private bool _connected;

        public MainForm()
        {
            Text = "Pyxis Sensor Data Exporter";
            Width = 780;
            Height = 600;
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(740, 560);
            BuildUi();
            RefreshPorts();
        }

        private void BuildUi()
        {
            // ---- 顶部连接区 ----
            var topPanel = new Panel { Dock = DockStyle.Top, Height = 36, Padding = new Padding(8, 6, 8, 0) };
            var lbl = new Label { Text = "COM:", AutoSize = true, Location = new Point(8, 10) };
            _portCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 120, Location = new Point(48, 7) };
            _refreshBtn = new Button { Text = "刷新", Width = 56, Location = new Point(176, 6) };
            _refreshBtn.Click += (s, e) => RefreshPorts();
            _connectBtn = new Button { Text = "连接", Width = 72, Location = new Point(240, 6) };
            _connectBtn.Click += OnConnectClick;
            _statusLabel = new Label { AutoSize = true, Location = new Point(322, 10), ForeColor = Color.Gray };
            topPanel.Controls.AddRange(new Control[] { lbl, _portCombo, _refreshBtn, _connectBtn, _statusLabel });
            Controls.Add(topPanel);

            // ---- 中部文件列表区 ----
            var listPanel = new Panel { Dock = DockStyle.Top, Height = 210, Padding = new Padding(8) };
            _listBtn = new Button { Text = "列出日志", Width = 90, Location = new Point(8, 4) };
            _listBtn.Click += OnListClick;
            _selectAllBtn = new Button { Text = "全选", Width = 64, Location = new Point(104, 4) };
            _selectAllBtn.Click += (s, e) => SetAllCheck(true);
            _selectNoneBtn = new Button { Text = "全不选", Width = 64, Location = new Point(174, 4) };
            _selectNoneBtn.Click += (s, e) => SetAllCheck(false);

            _grid = new DataGridView
            {
                Location = new Point(8, 34),
                Size = new Size(740, 168),
                AllowUserToAddRows = false,
                AllowUserToDeleteRows = false,
                AllowUserToResizeRows = false,
                AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
                ColumnHeadersDefaultCellStyle = new DataGridViewCellStyle { BackColor = SystemColors.ControlLight },
                SelectionMode = DataGridViewSelectionMode.FullRowSelect,
                ReadOnly = false,
                Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
            };
            _grid.Columns.Add(new DataGridViewCheckBoxColumn { Name = "Sel", HeaderText = "选", Width = 40, FillWeight = 30 });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "File", HeaderText = "文件名", FillWeight = 160, ReadOnly = true });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Size", HeaderText = "大小", Width = 80, FillWeight = 60, ReadOnly = true, DefaultCellStyle = new DataGridViewCellStyle { Alignment = DataGridViewContentAlignment.MiddleRight } });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Session", HeaderText = "Session", Width = 70, FillWeight = 50, ReadOnly = true });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "PN", HeaderText = "PN", Width = 70, FillWeight = 50, ReadOnly = true });
            listPanel.Controls.AddRange(new Control[] { _listBtn, _selectAllBtn, _selectNoneBtn, _grid });
            Controls.Add(listPanel);

            // ---- 下载区 ----
            var downloadPanel = new Panel { Dock = DockStyle.Top, Height = 80, Padding = new Padding(8) };
            var dirLabel = new Label { Text = "保存到:", AutoSize = true, Location = new Point(8, 8) };
            _dirBox = new TextBox { Width = 440, Location = new Point(64, 5), Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right };
            _browseBtn = new Button { Text = "浏览...", Width = 72, Location = new Point(512, 4), Anchor = AnchorStyles.Top | AnchorStyles.Right };
            _browseBtn.Click += OnBrowseClick;
            _downloadBtn = new Button { Text = "下载选中", Width = 96, Location = new Point(8, 36) };
            _downloadBtn.Click += OnDownloadClick;
            _openFolderBtn = new Button { Text = "打开目录", Width = 96, Location = new Point(110, 36) };
            _openFolderBtn.Click += (s, e) => OpenSaveFolder();
            _deleteBtn = new Button { Text = "删除选中", Width = 96, Location = new Point(212, 36), ForeColor = Color.DarkRed };
            _deleteBtn.Click += OnDeleteClick;
            _cancelBtn = new Button { Text = "取消", Width = 72, Location = new Point(314, 36), Enabled = false };
            _cancelBtn.Click += (s, e) => { if (_cts != null) _cts.Cancel(); };
            downloadPanel.Controls.AddRange(new Control[] { dirLabel, _dirBox, _browseBtn, _downloadBtn, _openFolderBtn, _deleteBtn, _cancelBtn });
            Controls.Add(downloadPanel);

            // ---- 进度区 ----
            var progPanel = new Panel { Dock = DockStyle.Top, Height = 52, Padding = new Padding(8, 0, 8, 4) };
            _currentFileLabel = new Label { Text = "", AutoSize = true, Location = new Point(8, 4) };
            _progressBar = new ProgressBar { Location = new Point(8, 24), Width = 600, Height = 18, Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right };
            _progressLabel = new Label { Text = "", AutoSize = true, Location = new Point(614, 26), Anchor = AnchorStyles.Top | AnchorStyles.Right };
            progPanel.Controls.AddRange(new Control[] { _currentFileLabel, _progressBar, _progressLabel });
            Controls.Add(progPanel);

            // ---- 日志区 ----
            _logBox = new TextBox
            {
                Dock = DockStyle.Fill,
                Multiline = true,
                ScrollBars = ScrollBars.Vertical,
                ReadOnly = true,
                Font = new Font(FontFamily.GenericMonospace, 8.25f),
                BackColor = Color.FromArgb(30, 30, 30),
                ForeColor = Color.FromArgb(220, 220, 220)
            };
            Controls.Add(_logBox);
            _logBox.BringToFront();

            // 默认保存目录
            _dirBox.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "sensor_logs");

            // 窗口关闭时断开
            FormClosing += (s, e) =>
            {
                if (_cts != null) _cts.Cancel();
                if (_client != null)
                {
                    _client.CloseSession();
                    _client.Dispose();
                }
            };
        }

        private void RefreshPorts()
        {
            _portCombo.Items.Clear();
            string[] ports = SerialPort.GetPortNames();
            foreach (string p in ports) _portCombo.Items.Add(p);
            if (_portCombo.Items.Count > 0) _portCombo.SelectedIndex = 0;
        }

        private void Log(string msg)
        {
            if (InvokeRequired) { BeginInvoke((Action)(() => Log(msg))); return; }
            _logBox.AppendText(DateTime.Now.ToString("HH:mm:ss") + " " + msg + Environment.NewLine);
        }

        // ---- 连接 / 断开 ----

        private void OnConnectClick(object sender, EventArgs e)
        {
            if (_connected)
            {
                DoDisconnect();
                return;
            }
            if (_portCombo.SelectedItem == null)
            {
                MessageBox.Show("请先选择 COM 端口");
                return;
            }
            try
            {
                _client = new EspLogClient();
                _client.Connect((string)_portCombo.SelectedItem);
                var info = _client.Ping();
                _connected = true;
                _connectBtn.Text = "断开";
                _statusLabel.Text = string.Format("已连接 ESPLOG1  cfg={0}  fw={1}", info[0], info[1]);
                _statusLabel.ForeColor = Color.Green;
                Log("连接成功: " + _statusLabel.Text);
            }
            catch (Exception ex)
            {
                if (_client != null) _client.Dispose();
                _client = null;
                _statusLabel.Text = "连接失败";
                _statusLabel.ForeColor = Color.Red;
                Log("连接失败: " + ex.Message);
            }
        }

        private void DoDisconnect()
        {
            if (_client != null)
            {
                _client.CloseSession();
                _client.Dispose();
                _client = null;
            }
            _connected = false;
            _connectBtn.Text = "连接";
            _statusLabel.Text = "已断开";
            _statusLabel.ForeColor = Color.Gray;
            Log("已断开");
        }

        // ---- 列出日志 ----

        private void OnListClick(object sender, EventArgs e)
        {
            if (!_connected) { MessageBox.Show("请先连接设备"); return; }
            try
            {
                var entries = _client.ListFiles();
                _grid.Rows.Clear();
                foreach (var en in entries)
                {
                    _grid.Rows.Add(false, en.FileName, FormatSize(en.Size), en.Session, en.Pn);
                    _grid.Rows[_grid.Rows.Count - 1].Tag = en;
                }
                Log(string.Format("列出 {0} 个日志文件", entries.Count));
            }
            catch (Exception ex) { Log("LIST 失败: " + ex.Message); }
        }

        // ---- 下载 ----

        private void OnDownloadClick(object sender, EventArgs e)
        {
            if (!_connected) { MessageBox.Show("请先连接设备"); return; }
            var selected = GetSelectedEntries();
            if (selected.Count == 0) { MessageBox.Show("请勾选要下载的文件"); return; }
            if (string.IsNullOrWhiteSpace(_dirBox.Text)) { MessageBox.Show("请选择保存目录"); return; }
            Directory.CreateDirectory(_dirBox.Text);

            _cts = new CancellationTokenSource();
            SetControlsBusy(true);

            Task.Run(() =>
            {
                var progress = new Progress<DownloadProgress>(p =>
                {
                    _currentFileLabel.Text = p.FileName ?? "";
                    if (p.Total > 0)
                    {
                        _progressBar.Maximum = 100;
                        _progressBar.Value = (int)(p.Offset * 100 / p.Total);
                        _progressLabel.Text = string.Format("{0} / {1}  {2}%",
                            FormatSize(p.Offset), FormatSize(p.Total), p.Offset * 100 / p.Total);
                    }
                    if (p.Done)
                        Log("完成: " + p.FileName + (p.Message != null ? " - " + p.Message : ""));
                });
                try
                {
                    int success = 0, failed = 0;
                    for (int i = 0; i < selected.Count; i++)
                    {
                        var en = selected[i];
                        _cts.Token.ThrowIfCancellationRequested();
                        BeginInvoke((Action)(() =>
                        {
                            _currentFileLabel.Text = string.Format("[{0}/{1}] {2}", i + 1, selected.Count, en.FileName);
                        }));
                        try
                        {
                            _client.DownloadFile(en.Path, Path.Combine(_dirBox.Text, en.FileName), progress, _cts.Token);
                            success++;
                        }
                        catch (OperationCanceledException) { throw; }
                        catch (Exception ex)
                        {
                            failed++;
                            Log(string.Format("文件 {0} 下载失败: {1}", en.FileName, ex.Message));
                        }
                    }
                    BeginInvoke((Action)(() =>
                    {
                        _progressBar.Value = 0;
                        _progressLabel.Text = string.Format("完成 {0} 个, 失败 {1} 个", success, failed);
                        _currentFileLabel.Text = "下载结束";
                    }));
                    Log(string.Format("下载结束: 成功 {0}, 失败 {1}", success, failed));
                }
                catch (OperationCanceledException) { Log("下载已取消"); }
                catch (Exception ex) { Log("下载失败: " + ex.Message); }
                finally
                {
                    BeginInvoke((Action)(() => SetControlsBusy(false)));
                }
            }, _cts.Token);
        }

        // ---- 删除 ----

        private void OnDeleteClick(object sender, EventArgs e)
        {
            if (!_connected) { MessageBox.Show("请先连接设备"); return; }
            var selected = GetSelectedEntries();
            if (selected.Count == 0) { MessageBox.Show("请勾选要删除的文件"); return; }
            if (MessageBox.Show(
                string.Format("确认删除设备上的 {0} 个日志文件？\n此操作不可恢复！", selected.Count),
                "删除确认", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;

            try
            {
                foreach (var en in selected)
                {
                    _client.Delete(en.Path);
                    Log("已删除: " + en.FileName);
                }
                OnListClick(null, null); // 刷新列表
            }
            catch (Exception ex) { Log("删除失败: " + ex.Message); }
        }

        // ---- 辅助 ----

        private List<LogFileEntry> GetSelectedEntries()
        {
            var list = new List<LogFileEntry>();
            foreach (DataGridViewRow row in _grid.Rows)
            {
                if (row.IsNewRow) continue;
                if (Convert.ToBoolean(row.Cells["Sel"].Value))
                {
                    var en = row.Tag as LogFileEntry;
                    if (en != null) list.Add(en);
                }
            }
            return list;
        }

        private void SetAllCheck(bool value)
        {
            foreach (DataGridViewRow row in _grid.Rows)
            {
                if (!row.IsNewRow) row.Cells["Sel"].Value = value;
            }
        }

        private void OnBrowseClick(object sender, EventArgs e)
        {
            using (var dlg = new FolderBrowserDialog())
            {
                if (Directory.Exists(_dirBox.Text)) dlg.SelectedPath = _dirBox.Text;
                if (dlg.ShowDialog(this) == DialogResult.OK)
                    _dirBox.Text = dlg.SelectedPath;
            }
        }

        private void OpenSaveFolder()
        {
            string dir = _dirBox.Text;
            if (Directory.Exists(dir))
                System.Diagnostics.Process.Start("explorer.exe", dir);
            else
                MessageBox.Show("目录不存在: " + dir);
        }

        private void SetControlsBusy(bool busy)
        {
            _downloadBtn.Enabled = !busy;
            _deleteBtn.Enabled = !busy;
            _listBtn.Enabled = !busy;
            _connectBtn.Enabled = !busy;
            _cancelBtn.Enabled = busy;
        }

        private static string FormatSize(long bytes)
        {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1048576) return (bytes / 1024.0).ToString("F1") + " KB";
            return (bytes / 1048576.0).ToString("F2") + " MB";
        }
    }
}
