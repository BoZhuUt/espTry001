using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class ProfileManagerProgram
{
    [STAThread]
    private static void Main(string[] args)
    {
        // Headless report mode is used by the build/test script and is also handy
        // when a report must be regenerated without opening the editor.
        if (args.Length >= 2 && args[0] == "--report")
        {
            string jsonPath = Path.GetFullPath(args[1]);
            string outputPath = args.Length >= 3
                ? Path.GetFullPath(args[2]) : Path.ChangeExtension(jsonPath, ".pdf");
            string language = args.Length >= 4 ? args[3] : "en";
            string json = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(jsonPath));
            IDictionary<string, object> root = new JavaScriptSerializer().DeserializeObject(json)
                as IDictionary<string, object>;
            if (root == null) throw new InvalidDataException("The JSON root must be an object.");
            List<SensorProfile> profiles = ProfileManagerForm.ParseProfiles(root);
            ProfilePdfReport.Generate(outputPath,
                Convert.ToString(root["config_version"], CultureInfo.InvariantCulture),
                profiles, language);
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new ProfileManagerForm(args.Length > 0 ? args[0] : null));
    }
}

// Creates a self-contained PDF without a third-party PDF package. Each A4 page
// is rendered with GDI+ first, then embedded as JPEG. This deliberately trades
// selectable text for reliable Chinese/Spanish rendering on every recipient PC.
internal static class ProfilePdfReport
{
    private const int PageWidth = 1240;
    private const int PageHeight = 1754;
    private const int Margin = 60;

    private sealed class PagePainter : IDisposable
    {
        internal readonly Bitmap Bitmap;
        internal readonly Graphics Graphics;
        internal int Y;
        internal readonly Font TitleFont;
        internal readonly Font HeadingFont;
        internal readonly Font NormalFont;
        internal readonly Font SmallFont;

        internal PagePainter(string language, string continuation)
        {
            Bitmap = new Bitmap(PageWidth, PageHeight, PixelFormat.Format24bppRgb);
            Graphics = Graphics.FromImage(Bitmap);
            Graphics.Clear(Color.White);
            Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            string family = language == "zh" ? "Microsoft YaHei" : "Segoe UI";
            TitleFont = new Font(family, 26F, FontStyle.Bold, GraphicsUnit.Pixel);
            HeadingFont = new Font(family, 20F, FontStyle.Bold, GraphicsUnit.Pixel);
            NormalFont = new Font(family, 17F, FontStyle.Regular, GraphicsUnit.Pixel);
            SmallFont = new Font(family, 15F, FontStyle.Regular, GraphicsUnit.Pixel);
            Y = Margin;
            if (!string.IsNullOrEmpty(continuation))
            {
                Graphics.DrawString(continuation, HeadingFont, Brushes.DarkSlateBlue,
                    new PointF(Margin, Y));
                Y += 44;
            }
        }

        internal bool CanFit(int height)
        {
            return Y + height <= PageHeight - Margin;
        }

        internal void DrawText(string value, Font font, Brush brush, int height)
        {
            Graphics.DrawString(value, font, brush,
                new RectangleF(Margin, Y, PageWidth - Margin * 2, height));
            Y += height;
        }

        internal void DrawTableRow(string[] cells, int[] widths, bool header, int height)
        {
            int x = Margin;
            using (Pen border = new Pen(Color.FromArgb(170, 175, 185), 1F))
            using (Brush fill = new SolidBrush(header
                ? Color.FromArgb(220, 229, 242) : Color.White))
            using (StringFormat format = new StringFormat())
            {
                format.LineAlignment = StringAlignment.Center;
                format.Trimming = StringTrimming.EllipsisCharacter;
                format.FormatFlags = StringFormatFlags.NoWrap;
                for (int i = 0; i < cells.Length; ++i)
                {
                    Rectangle rect = new Rectangle(x, Y, widths[i], height);
                    Graphics.FillRectangle(fill, rect);
                    Graphics.DrawRectangle(border, rect);
                    Rectangle textRect = new Rectangle(x + 7, Y + 2, widths[i] - 14, height - 4);
                    Graphics.DrawString(cells[i] ?? "", header ? HeadingFont : SmallFont,
                        Brushes.Black, textRect, format);
                    x += widths[i];
                }
            }
            Y += height;
        }

        internal byte[] Finish()
        {
            using (MemoryStream stream = new MemoryStream())
            {
                Bitmap.Save(stream, ImageFormat.Jpeg);
                return stream.ToArray();
            }
        }

        public void Dispose()
        {
            SmallFont.Dispose();
            NormalFont.Dispose();
            HeadingFont.Dispose();
            TitleFont.Dispose();
            Graphics.Dispose();
            Bitmap.Dispose();
        }
    }

    internal static void Generate(string outputPath, string version,
                                  IList<SensorProfile> profiles, string language)
    {
        if (language != "zh" && language != "es") language = "en";
        List<byte[]> pages = new List<byte[]>();
        int[] summaryWidths = { 230, 530, 180, 180 };
        int[] fieldWidths = { 300, 80, 200, 180, 180, 180 };

        using (PagePainter page = new PagePainter(language, ""))
        {
            page.DrawText(R(language, "ReportTitle"), page.TitleFont,
                Brushes.DarkSlateBlue, 58);
            page.DrawText(R(language, "Version") + ": " + version + "    " +
                R(language, "Generated") + ": " + DateTime.Now.ToString("yyyy-MM-dd HH:mm"),
                page.NormalFont, Brushes.Black, 42);
            page.Y += 12;
            page.DrawText(R(language, "SupportedList"), page.HeadingFont,
                Brushes.Black, 42);
            page.DrawTableRow(new string[] { "PN", R(language, "DeviceName"),
                R(language, "FieldCount"), R(language, "RegisterCount") },
                summaryWidths, true, 42);
            foreach (SensorProfile profile in profiles)
            {
                int registers = 0;
                foreach (ProfileField field in profile.Fields)
                    registers += field.ValueType == "float32" ? 2 : 1;
                page.DrawTableRow(new string[] {
                    profile.ProductNum.ToString(CultureInfo.InvariantCulture), profile.Name,
                    profile.Fields.Count.ToString(CultureInfo.InvariantCulture),
                    registers.ToString(CultureInfo.InvariantCulture)
                }, summaryWidths, false, 38);
            }
            pages.Add(page.Finish());
        }

        PagePainter detail = null;
        try
        {
            foreach (SensorProfile profile in profiles)
            {
                int required = 92 + 42 + profile.Fields.Count * 38 + 24;
                if (detail == null || !detail.CanFit(required))
                {
                    if (detail != null)
                    {
                        pages.Add(detail.Finish());
                        detail.Dispose();
                    }
                    detail = new PagePainter(language, R(language, "RegisterDetails"));
                }

                detail.DrawText("PN " + profile.ProductNum + "  |  " + profile.Name,
                    detail.HeadingFont, Brushes.DarkSlateBlue, 48);
                detail.DrawText(R(language, "Fields") + ": " + profile.Fields.Count,
                    detail.NormalFont, Brushes.Black, 34);
                detail.DrawTableRow(new string[] { R(language, "Parameter"), "FC",
                    R(language, "Register"), R(language, "Type"),
                    R(language, "WordOrder"), R(language, "Decimals") },
                    fieldWidths, true, 42);
                foreach (ProfileField field in profile.Fields)
                {
                    int end = field.Address + (field.ValueType == "float32" ? 1 : 0);
                    string address = end == field.Address
                        ? field.Address.ToString(CultureInfo.InvariantCulture)
                        : field.Address + "-" + end;
                    detail.DrawTableRow(new string[] { field.Label, "03", address,
                        field.ValueType, field.ValueType == "float32" ? "CDAB" : "-",
                        field.Decimals.ToString(CultureInfo.InvariantCulture) },
                        fieldWidths, false, 38);
                }
                detail.Y += 24;
            }
            if (detail != null) pages.Add(detail.Finish());
        }
        finally
        {
            if (detail != null) detail.Dispose();
        }

        WritePdf(outputPath, pages);
    }

    private static string R(string language, string key)
    {
        if (language == "zh")
        {
            switch (key)
            {
                case "ReportTitle": return "传感器支持配置报告";
                case "Version": return "配置版本";
                case "Generated": return "生成时间";
                case "SupportedList": return "支持的设备列表";
                case "DeviceName": return "设备名称";
                case "FieldCount": return "参数数量";
                case "RegisterCount": return "寄存器数量";
                case "RegisterDetails": return "具体读取寄存器";
                case "Fields": return "参数";
                case "Parameter": return "参数名称";
                case "Register": return "寄存器";
                case "Type": return "类型";
                case "WordOrder": return "字序";
                case "Decimals": return "小数位";
            }
        }
        if (language == "es")
        {
            switch (key)
            {
                case "ReportTitle": return "Informe de perfiles de sensores";
                case "Version": return "Versión";
                case "Generated": return "Generado";
                case "SupportedList": return "Dispositivos compatibles";
                case "DeviceName": return "Nombre del dispositivo";
                case "FieldCount": return "Parámetros";
                case "RegisterCount": return "Registros";
                case "RegisterDetails": return "Detalle de registros leídos";
                case "Fields": return "Parámetros";
                case "Parameter": return "Parámetro";
                case "Register": return "Registro";
                case "Type": return "Tipo";
                case "WordOrder": return "Orden";
                case "Decimals": return "Decimales";
            }
        }
        switch (key)
        {
            case "ReportTitle": return "Sensor Profile Support Report";
            case "Version": return "Config version";
            case "Generated": return "Generated";
            case "SupportedList": return "Supported devices";
            case "DeviceName": return "Device name";
            case "FieldCount": return "Fields";
            case "RegisterCount": return "Registers";
            case "RegisterDetails": return "Registers read by profile";
            case "Fields": return "Fields";
            case "Parameter": return "Parameter";
            case "Register": return "Register";
            case "Type": return "Type";
            case "WordOrder": return "Word order";
            case "Decimals": return "Decimals";
        }
        return key;
    }

    private static void WritePdf(string outputPath, IList<byte[]> pages)
    {
        using (MemoryStream pdf = new MemoryStream())
        {
            List<long> offsets = new List<long>();
            offsets.Add(0);
            WriteAscii(pdf, "%PDF-1.4\n");
            BeginObject(pdf, offsets, 1);
            WriteAscii(pdf, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

            BeginObject(pdf, offsets, 2);
            StringBuilder kids = new StringBuilder();
            for (int i = 0; i < pages.Count; ++i)
                kids.Append(3 + i * 3).Append(" 0 R ");
            WriteAscii(pdf, "<< /Type /Pages /Count " + pages.Count +
                " /Kids [ " + kids + "] >>\nendobj\n");

            for (int i = 0; i < pages.Count; ++i)
            {
                int pageObject = 3 + i * 3;
                int imageObject = pageObject + 1;
                int contentObject = pageObject + 2;
                BeginObject(pdf, offsets, pageObject);
                WriteAscii(pdf, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] " +
                    "/Resources << /XObject << /Im0 " + imageObject + " 0 R >> >> " +
                    "/Contents " + contentObject + " 0 R >>\nendobj\n");

                BeginObject(pdf, offsets, imageObject);
                WriteAscii(pdf, "<< /Type /XObject /Subtype /Image /Width " + PageWidth +
                    " /Height " + PageHeight + " /ColorSpace /DeviceRGB /BitsPerComponent 8 " +
                    "/Filter /DCTDecode /Length " + pages[i].Length + " >>\nstream\n");
                pdf.Write(pages[i], 0, pages[i].Length);
                WriteAscii(pdf, "\nendstream\nendobj\n");

                byte[] content = Encoding.ASCII.GetBytes("q 595 0 0 842 0 0 cm /Im0 Do Q\n");
                BeginObject(pdf, offsets, contentObject);
                WriteAscii(pdf, "<< /Length " + content.Length + " >>\nstream\n");
                pdf.Write(content, 0, content.Length);
                WriteAscii(pdf, "endstream\nendobj\n");
            }

            long xref = pdf.Position;
            int objectCount = 2 + pages.Count * 3;
            WriteAscii(pdf, "xref\n0 " + (objectCount + 1) + "\n");
            WriteAscii(pdf, "0000000000 65535 f \n");
            for (int i = 1; i <= objectCount; ++i)
                WriteAscii(pdf, offsets[i].ToString("0000000000", CultureInfo.InvariantCulture) +
                    " 00000 n \n");
            WriteAscii(pdf, "trailer\n<< /Size " + (objectCount + 1) +
                " /Root 1 0 R >>\nstartxref\n" + xref + "\n%%EOF\n");
            File.WriteAllBytes(outputPath, pdf.ToArray());
        }
    }

    private static void BeginObject(Stream stream, List<long> offsets, int number)
    {
        while (offsets.Count <= number) offsets.Add(0);
        offsets[number] = stream.Position;
        WriteAscii(stream, number + " 0 obj\n");
    }

    private static void WriteAscii(Stream stream, string value)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(value);
        stream.Write(bytes, 0, bytes.Length);
    }
}

internal sealed class ProfileField
{
    public string Key { get; set; }
    public string Label { get; set; }
    public int Function { get; set; }
    public int Address { get; set; }
    public int AddressBase { get; set; }
    public string ValueType { get; set; }
    public string WordOrder { get; set; }
    public int Decimals { get; set; }

    public ProfileField Clone()
    {
        return (ProfileField)MemberwiseClone();
    }
}

internal sealed class SensorProfile
{
    public uint ProductNum { get; set; }
    public string Name { get; set; }
    public BindingList<ProfileField> Fields { get; set; }

    public SensorProfile Clone()
    {
        SensorProfile copy = new SensorProfile();
        copy.ProductNum = ProductNum;
        copy.Name = Name;
        copy.Fields = new BindingList<ProfileField>();
        foreach (ProfileField field in Fields)
            copy.Fields.Add(field.Clone());
        return copy;
    }
}

internal sealed class ProfileManagerForm : Form
{
    private const int MaxProfiles = 50;
    private const int MaxFields = 16;
    private const int MaxJsonBytes = 256 * 1024;

    private readonly List<SensorProfile> profiles = new List<SensorProfile>();
    private readonly ListBox profileList = new ListBox();
    private readonly TextBox productNumText = new TextBox();
    private readonly TextBox nameText = new TextBox();
    private readonly TextBox versionText = new TextBox();
    private readonly DataGridView fieldGrid = new DataGridView();
    private readonly ToolStripStatusLabel statusLabel = new ToolStripStatusLabel();
    private readonly ToolStripButton newToolButton = new ToolStripButton();
    private readonly ToolStripButton openToolButton = new ToolStripButton();
    private readonly ToolStripButton saveToolButton = new ToolStripButton();
    private readonly ToolStripButton saveAsToolButton = new ToolStripButton();
    private readonly ToolStripButton validateToolButton = new ToolStripButton();
    private readonly ToolStripLabel versionCaption = new ToolStripLabel();
    private readonly ToolStripLabel languageCaption = new ToolStripLabel();
    private readonly ToolStripComboBox languageCombo = new ToolStripComboBox();
    private readonly Label supportedProductsCaption = new Label();
    private readonly Label profileDetailsCaption = new Label();
    private readonly Label productPnCaption = new Label();
    private readonly Label nameCaption = new Label();
    private readonly Label fieldHintCaption = new Label();
    private readonly Button addProfileButton = new Button();
    private readonly Button duplicateProfileButton = new Button();
    private readonly Button deleteProfileButton = new Button();
    private readonly Button addFieldButton = new Button();
    private readonly Button deleteFieldButton = new Button();
    private string currentPath;
    private string language;
    private int currentProfileIndex = -1;
    private bool switchingProfile;
    private bool dirty;

    internal ProfileManagerForm(string initialPath)
    {
        string systemLanguage = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
        language = systemLanguage == "zh" ? "zh" : (systemLanguage == "es" ? "es" : "en");
        Text = "Sensor Profile Manager";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1120, 680);
        MinimumSize = new Size(960, 600);
        Font = new Font("Segoe UI", 9.5F);

        ToolStrip tool = BuildToolbar();
        Controls.Add(tool);

        SplitContainer split = new SplitContainer();
        split.Dock = DockStyle.Fill;
        split.Location = new Point(0, tool.Height);
        split.SplitterDistance = 260;
        split.FixedPanel = FixedPanel.Panel1;
        split.Panel1.Padding = new Padding(10);
        split.Panel2.Padding = new Padding(10);
        Controls.Add(split);
        split.BringToFront();

        BuildProfilePanel(split.Panel1);
        BuildEditorPanel(split.Panel2);

        StatusStrip status = new StatusStrip();
        status.Items.Add(statusLabel);
        Controls.Add(status);
        status.BringToFront();

        FormClosing += FormIsClosing;

        ApplyLanguage();

        currentPath = !string.IsNullOrEmpty(initialPath) ? initialPath : GetDefaultJsonPath();
        if (File.Exists(currentPath))
            LoadDocument(currentPath);
        else
            NewDocument();
    }

    private ToolStrip BuildToolbar()
    {
        ToolStrip tool = new ToolStrip();
        tool.GripStyle = ToolStripGripStyle.Hidden;
        tool.Dock = DockStyle.Top;
        ConfigureToolButton(newToolButton, delegate { NewRequested(); });
        ConfigureToolButton(openToolButton, delegate { OpenRequested(); });
        ConfigureToolButton(saveToolButton, delegate { SaveDocument(false); });
        ConfigureToolButton(saveAsToolButton, delegate { SaveDocument(true); });
        tool.Items.Add(newToolButton);
        tool.Items.Add(openToolButton);
        tool.Items.Add(saveToolButton);
        tool.Items.Add(saveAsToolButton);
        tool.Items.Add(new ToolStripSeparator());
        ConfigureToolButton(validateToolButton, delegate { ValidateRequested(); });
        tool.Items.Add(validateToolButton);
        tool.Items.Add(new ToolStripSeparator());
        tool.Items.Add(versionCaption);
        ToolStripControlHost versionHost = new ToolStripControlHost(versionText);
        versionText.Width = 145;
        versionText.TextChanged += delegate { if (!switchingProfile) MarkDirty(); };
        tool.Items.Add(versionHost);
        tool.Items.Add(new ToolStripSeparator());
        tool.Items.Add(languageCaption);
        languageCombo.DropDownStyle = ComboBoxStyle.DropDownList;
        languageCombo.AutoSize = false;
        languageCombo.Width = 100;
        languageCombo.Items.AddRange(new object[] { "中文", "English", "Español" });
        languageCombo.SelectedIndex = language == "zh" ? 0 : (language == "es" ? 2 : 1);
        languageCombo.SelectedIndexChanged += delegate
        {
            language = languageCombo.SelectedIndex == 0 ? "zh" :
                (languageCombo.SelectedIndex == 2 ? "es" : "en");
            ApplyLanguage();
        };
        tool.Items.Add(languageCombo);
        return tool;
    }

    private static void ConfigureToolButton(ToolStripButton button, EventHandler click)
    {
        button.DisplayStyle = ToolStripItemDisplayStyle.Text;
        button.Click += click;
    }

    private void BuildProfilePanel(Control panel)
    {
        supportedProductsCaption.Font = new Font(Font, FontStyle.Bold);
        supportedProductsCaption.Dock = DockStyle.Top;
        supportedProductsCaption.Height = 28;
        panel.Controls.Add(supportedProductsCaption);

        FlowLayoutPanel buttons = new FlowLayoutPanel();
        buttons.Dock = DockStyle.Bottom;
        buttons.Height = 42;
        buttons.FlowDirection = FlowDirection.LeftToRight;
        buttons.WrapContents = false;
        buttons.Padding = new Padding(0, 4, 0, 0);
        ConfigureButton(addProfileButton, AddProfile, 58);
        ConfigureButton(duplicateProfileButton, DuplicateProfile, 82);
        ConfigureButton(deleteProfileButton, DeleteProfile, 62);
        buttons.Controls.Add(addProfileButton);
        buttons.Controls.Add(duplicateProfileButton);
        buttons.Controls.Add(deleteProfileButton);
        panel.Controls.Add(buttons);

        profileList.Dock = DockStyle.Fill;
        profileList.IntegralHeight = false;
        profileList.SelectedIndexChanged += ProfileSelectionChanged;
        panel.Controls.Add(profileList);
        profileList.BringToFront();
    }

    private void BuildEditorPanel(Control panel)
    {
        TableLayoutPanel layout = new TableLayoutPanel();
        layout.Dock = DockStyle.Fill;
        layout.ColumnCount = 4;
        layout.RowCount = 4;
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 65));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.Controls.Add(layout);

        profileDetailsCaption.Font = new Font("Segoe UI", 13F, FontStyle.Bold);
        profileDetailsCaption.Dock = DockStyle.Fill;
        profileDetailsCaption.TextAlign = ContentAlignment.MiddleLeft;
        layout.Controls.Add(profileDetailsCaption, 0, 0);
        layout.SetColumnSpan(profileDetailsCaption, 4);

        ConfigureFieldLabel(productPnCaption);
        layout.Controls.Add(productPnCaption, 0, 1);
        productNumText.Dock = DockStyle.Fill;
        productNumText.TextChanged += HeaderChanged;
        layout.Controls.Add(productNumText, 1, 1);
        ConfigureFieldLabel(nameCaption);
        layout.Controls.Add(nameCaption, 2, 1);
        nameText.Dock = DockStyle.Fill;
        nameText.TextChanged += HeaderChanged;
        layout.Controls.Add(nameText, 3, 1);

        FlowLayoutPanel fieldButtons = new FlowLayoutPanel();
        fieldButtons.Dock = DockStyle.Fill;
        ConfigureButton(addFieldButton, AddField, 125);
        ConfigureButton(deleteFieldButton, DeleteField, 140);
        fieldButtons.Controls.Add(addFieldButton);
        fieldButtons.Controls.Add(deleteFieldButton);
        fieldHintCaption.AutoSize = true;
        fieldHintCaption.Margin = new Padding(14, 8, 0, 0);
        fieldButtons.Controls.Add(fieldHintCaption);
        layout.Controls.Add(fieldButtons, 0, 2);
        layout.SetColumnSpan(fieldButtons, 4);

        ConfigureGrid();
        layout.Controls.Add(fieldGrid, 0, 3);
        layout.SetColumnSpan(fieldGrid, 4);
    }

    private void ConfigureGrid()
    {
        fieldGrid.Dock = DockStyle.Fill;
        fieldGrid.AutoGenerateColumns = false;
        fieldGrid.AllowUserToAddRows = false;
        fieldGrid.AllowUserToDeleteRows = false;
        fieldGrid.MultiSelect = true;
        fieldGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        fieldGrid.RowHeadersVisible = false;
        fieldGrid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
        fieldGrid.DataError += delegate(object sender, DataGridViewDataErrorEventArgs e)
        {
            e.ThrowException = false;
        };
        fieldGrid.CellValueChanged += delegate { if (!switchingProfile) MarkDirty(); };

        AddTextColumn("Key", "Key", 120);
        AddTextColumn("Label", "Label", 120);
        AddNumberColumn("FC", "Function", 42);
        AddNumberColumn("Address", "Address", 75);
        AddNumberColumn("Base", "AddressBase", 45);

        DataGridViewComboBoxColumn type = new DataGridViewComboBoxColumn();
        type.HeaderText = "Type";
        type.DataPropertyName = "ValueType";
        type.Items.AddRange("float32", "uint16");
        type.FillWeight = 70;
        fieldGrid.Columns.Add(type);

        DataGridViewComboBoxColumn order = new DataGridViewComboBoxColumn();
        order.HeaderText = "Word order";
        order.DataPropertyName = "WordOrder";
        order.Items.AddRange("CDAB", "-");
        order.FillWeight = 70;
        fieldGrid.Columns.Add(order);
        AddNumberColumn("Decimals", "Decimals", 58);
    }

    private void AddTextColumn(string header, string property, float weight)
    {
        DataGridViewTextBoxColumn column = new DataGridViewTextBoxColumn();
        column.HeaderText = header;
        column.DataPropertyName = property;
        column.FillWeight = weight;
        fieldGrid.Columns.Add(column);
    }

    private void AddNumberColumn(string header, string property, float weight)
    {
        AddTextColumn(header, property, weight);
    }

    private static void ConfigureFieldLabel(Label label)
    {
        label.Dock = DockStyle.Fill;
        label.TextAlign = ContentAlignment.MiddleLeft;
    }

    private static void ConfigureButton(Button button, EventHandler click, int width)
    {
        button.Width = width;
        button.Height = 30;
        button.Click += click;
    }

    private static string GetDefaultJsonPath()
    {
        string toolsDirectory = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        DirectoryInfo parent = Directory.GetParent(toolsDirectory);
        return parent == null
            ? "sensor_profiles.json"
            : Path.Combine(parent.FullName, "config", "sensor_profiles.json");
    }

    private void LoadDocument(string path)
    {
        try
        {
            string json = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
            IDictionary<string, object> root = new JavaScriptSerializer().DeserializeObject(json)
                as IDictionary<string, object>;
            if (root == null)
                throw new InvalidDataException("The JSON root must be an object.");
            if (!root.ContainsKey("schema_version") || Convert.ToInt32(root["schema_version"]) != 1)
                throw new InvalidDataException("schema_version must be 1.");

            List<SensorProfile> loaded = ParseProfiles(root);
            profiles.Clear();
            profiles.AddRange(loaded);
            switchingProfile = true;
            versionText.Text = Convert.ToString(root["config_version"], CultureInfo.InvariantCulture);
            switchingProfile = false;
            currentPath = Path.GetFullPath(path);
            currentProfileIndex = -1;
            RebuildProfileList(loaded.Count > 0 ? 0 : -1);
            dirty = false;
            UpdateCaption();
            SetStatus(string.Format(T("LoadedStatus"), loaded.Count, currentPath));
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, T("OpenFailed"), MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    internal static List<SensorProfile> ParseProfiles(IDictionary<string, object> root)
    {
        if (!root.ContainsKey("config_version") || !root.ContainsKey("profiles"))
            throw new InvalidDataException("Missing config_version or profiles.");
        object[] rawProfiles = root["profiles"] as object[];
        if (rawProfiles == null)
            throw new InvalidDataException("profiles must be an array.");

        List<SensorProfile> result = new List<SensorProfile>();
        foreach (object rawEntry in rawProfiles)
        {
            IDictionary<string, object> entry = rawEntry as IDictionary<string, object>;
            if (entry == null)
                throw new InvalidDataException("A profile must be an object.");
            SensorProfile profile = new SensorProfile();
            profile.ProductNum = Convert.ToUInt32(entry["product_num"], CultureInfo.InvariantCulture);
            profile.Name = Convert.ToString(entry["name"], CultureInfo.InvariantCulture);
            profile.Fields = new BindingList<ProfileField>();
            object[] rawFields = entry["fields"] as object[];
            if (rawFields == null)
                throw new InvalidDataException("Profile fields must be an array.");
            foreach (object rawField in rawFields)
            {
                IDictionary<string, object> item = rawField as IDictionary<string, object>;
                ProfileField field = new ProfileField();
                field.Key = Convert.ToString(item["key"], CultureInfo.InvariantCulture);
                field.Label = Convert.ToString(item["label"], CultureInfo.InvariantCulture);
                field.Function = Convert.ToInt32(item["function"], CultureInfo.InvariantCulture);
                field.Address = Convert.ToInt32(item["address"], CultureInfo.InvariantCulture);
                field.AddressBase = Convert.ToInt32(item["address_base"], CultureInfo.InvariantCulture);
                field.ValueType = Convert.ToString(item["type"], CultureInfo.InvariantCulture);
                field.WordOrder = item.ContainsKey("word_order")
                    ? Convert.ToString(item["word_order"], CultureInfo.InvariantCulture) : "-";
                field.Decimals = Convert.ToInt32(item["decimals"], CultureInfo.InvariantCulture);
                profile.Fields.Add(field);
            }
            result.Add(profile);
        }
        return result;
    }

    private void NewRequested()
    {
        if (!ConfirmDiscard())
            return;
        NewDocument();
    }

    private void NewDocument()
    {
        profiles.Clear();
        profiles.Add(CreateEmptyProfile(1));
        currentPath = GetDefaultJsonPath();
        switchingProfile = true;
        versionText.Text = DateTime.Now.ToString("yyyy.MM.dd.1");
        switchingProfile = false;
        currentProfileIndex = -1;
        RebuildProfileList(0);
        dirty = true;
        UpdateCaption();
        SetStatus(T("NewConfiguration"));
    }

    private void OpenRequested()
    {
        if (!ConfirmDiscard())
            return;
        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Filter = T("JsonFilter");
            dialog.FileName = currentPath;
            if (dialog.ShowDialog(this) == DialogResult.OK)
                LoadDocument(dialog.FileName);
        }
    }

    private bool SaveDocument(bool saveAs)
    {
        try
        {
            CommitCurrentProfile();
            ValidateAll();
            if (saveAs || string.IsNullOrEmpty(currentPath))
            {
                using (SaveFileDialog dialog = new SaveFileDialog())
                {
                    dialog.Filter = T("JsonFilter");
                    dialog.FileName = string.IsNullOrEmpty(currentPath)
                        ? "sensor_profiles.json" : currentPath;
                    if (dialog.ShowDialog(this) != DialogResult.OK)
                        return false;
                    currentPath = dialog.FileName;
                }
            }
            string json = BuildJson();
            byte[] bytes = new UTF8Encoding(false).GetBytes(json);
            if (bytes.Length > MaxJsonBytes)
                throw new InvalidDataException(T("ErrJsonSize"));
            File.WriteAllBytes(currentPath, bytes);
            string pdfPath = Path.ChangeExtension(currentPath, ".pdf");
            ProfilePdfReport.Generate(pdfPath, versionText.Text.Trim(), profiles, language);
            dirty = false;
            UpdateCaption();
            SetStatus(string.Format(T("SavedStatus"), profiles.Count, bytes.Length,
                Path.GetFileName(pdfPath)));
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, T("SaveFailed"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    private void ValidateRequested()
    {
        try
        {
            CommitCurrentProfile();
            ValidateAll();
            int fields = 0;
            foreach (SensorProfile profile in profiles)
                fields += profile.Fields.Count;
            SetStatus(string.Format(T("ValidStatus"), profiles.Count, fields));
            MessageBox.Show(this, T("ConfigurationValid"), Text,
                MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, T("ValidationFailed"),
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void AddProfile(object sender, EventArgs e)
    {
        CommitCurrentProfile();
        if (profiles.Count >= MaxProfiles)
        {
            MessageBox.Show(this, T("MaxProfiles"));
            return;
        }
        uint pn = FindUnusedPn();
        profiles.Add(CreateEmptyProfile(pn));
        MarkDirty();
        RebuildProfileList(profiles.Count - 1);
    }

    private void DuplicateProfile(object sender, EventArgs e)
    {
        CommitCurrentProfile();
        if (currentProfileIndex < 0 || profiles.Count >= MaxProfiles)
            return;
        SensorProfile copy = profiles[currentProfileIndex].Clone();
        copy.ProductNum = FindUnusedPn();
        copy.Name += " COPY";
        profiles.Insert(currentProfileIndex + 1, copy);
        MarkDirty();
        RebuildProfileList(currentProfileIndex + 1);
    }

    private void DeleteProfile(object sender, EventArgs e)
    {
        if (currentProfileIndex < 0)
            return;
        if (profiles.Count == 1)
        {
            MessageBox.Show(this, T("AtLeastOneProfile"));
            return;
        }
        SensorProfile target = profiles[currentProfileIndex];
        if (MessageBox.Show(this, string.Format(T("DeletePn"), target.ProductNum), T("ConfirmDelete"),
            MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            return;
        int next = Math.Min(currentProfileIndex, profiles.Count - 2);
        profiles.RemoveAt(currentProfileIndex);
        currentProfileIndex = -1;
        MarkDirty();
        RebuildProfileList(next);
    }

    private void AddField(object sender, EventArgs e)
    {
        if (currentProfileIndex < 0)
            return;
        BindingList<ProfileField> fields = profiles[currentProfileIndex].Fields;
        if (fields.Count >= MaxFields)
        {
            MessageBox.Show(this, T("MaxFields"));
            return;
        }
        fields.Add(new ProfileField
        {
            Key = "newField",
            Label = "newField",
            Function = 3,
            Address = 40001,
            AddressBase = 1,
            ValueType = "uint16",
            WordOrder = "-",
            Decimals = 0
        });
        MarkDirty();
    }

    private void DeleteField(object sender, EventArgs e)
    {
        if (currentProfileIndex < 0)
            return;
        BindingList<ProfileField> fields = profiles[currentProfileIndex].Fields;
        if (fields.Count <= 1)
        {
            MessageBox.Show(this, T("AtLeastOneField"));
            return;
        }
        List<int> rows = new List<int>();
        foreach (DataGridViewRow row in fieldGrid.SelectedRows)
            if (!row.IsNewRow) rows.Add(row.Index);
        rows.Sort();
        rows.Reverse();
        foreach (int index in rows)
            if (index >= 0 && index < fields.Count && fields.Count > 1)
                fields.RemoveAt(index);
        MarkDirty();
    }

    private static SensorProfile CreateEmptyProfile(uint pn)
    {
        SensorProfile profile = new SensorProfile();
        profile.ProductNum = pn;
        profile.Name = "NEW-SENSOR";
        profile.Fields = new BindingList<ProfileField>();
        profile.Fields.Add(new ProfileField
        {
            Key = "sensorValue", Label = "sensorValue", Function = 3,
            Address = 46001, AddressBase = 1, ValueType = "float32",
            WordOrder = "CDAB", Decimals = 3
        });
        return profile;
    }

    private uint FindUnusedPn()
    {
        HashSet<uint> used = new HashSet<uint>();
        foreach (SensorProfile profile in profiles)
            used.Add(profile.ProductNum);
        uint value = 1;
        while (used.Contains(value)) value++;
        return value;
    }

    private void ProfileSelectionChanged(object sender, EventArgs e)
    {
        if (switchingProfile)
            return;
        CommitCurrentProfile();
        currentProfileIndex = profileList.SelectedIndex;
        LoadCurrentProfile();
    }

    private void LoadCurrentProfile()
    {
        switchingProfile = true;
        if (currentProfileIndex >= 0 && currentProfileIndex < profiles.Count)
        {
            SensorProfile profile = profiles[currentProfileIndex];
            productNumText.Text = profile.ProductNum.ToString(CultureInfo.InvariantCulture);
            nameText.Text = profile.Name;
            fieldGrid.DataSource = profile.Fields;
        }
        else
        {
            productNumText.Clear();
            nameText.Clear();
            fieldGrid.DataSource = null;
        }
        switchingProfile = false;
    }

    private void CommitCurrentProfile()
    {
        fieldGrid.EndEdit();
        if (currentProfileIndex < 0 || currentProfileIndex >= profiles.Count)
            return;
        uint pn;
        if (!uint.TryParse(productNumText.Text.Trim(), out pn) || pn == 0)
            pn = 0;
        profiles[currentProfileIndex].ProductNum = pn;
        profiles[currentProfileIndex].Name = nameText.Text.Trim();
    }

    private void HeaderChanged(object sender, EventArgs e)
    {
        if (switchingProfile || currentProfileIndex < 0)
            return;
        uint pn;
        if (uint.TryParse(productNumText.Text.Trim(), out pn) && pn > 0)
            profiles[currentProfileIndex].ProductNum = pn;
        else
            profiles[currentProfileIndex].ProductNum = 0;
        profiles[currentProfileIndex].Name = nameText.Text.Trim();
        MarkDirty();
        RefreshProfileListText();
    }

    private void RebuildProfileList(int selectedIndex)
    {
        switchingProfile = true;
        profileList.Items.Clear();
        foreach (SensorProfile profile in profiles)
            profileList.Items.Add(ProfileDisplay(profile));
        profileList.SelectedIndex = selectedIndex >= 0 && selectedIndex < profiles.Count
            ? selectedIndex : -1;
        currentProfileIndex = profileList.SelectedIndex;
        switchingProfile = false;
        LoadCurrentProfile();
    }

    private void RefreshProfileListText()
    {
        int selected = currentProfileIndex;
        switchingProfile = true;
        for (int i = 0; i < profiles.Count; ++i)
            profileList.Items[i] = ProfileDisplay(profiles[i]);
        profileList.SelectedIndex = selected;
        switchingProfile = false;
    }

    private static string ProfileDisplay(SensorProfile profile)
    {
        return profile.ProductNum + "  |  " + profile.Name;
    }

    private void ValidateAll()
    {
        fieldGrid.EndEdit();
        if (Encoding.UTF8.GetByteCount(versionText.Text.Trim()) == 0 ||
            Encoding.UTF8.GetByteCount(versionText.Text.Trim()) > 23)
            throw new InvalidDataException(T("ErrVersion"));
        if (profiles.Count == 0 || profiles.Count > MaxProfiles)
            throw new InvalidDataException(T("ErrProfileCount"));

        HashSet<uint> productNumbers = new HashSet<uint>();
        for (int i = 0; i < profiles.Count; ++i)
        {
            SensorProfile profile = profiles[i];
            string context = "PN " + profile.ProductNum;
            if (profile.ProductNum == 0)
                throw new InvalidDataException(T("ErrPnRange"));
            if (!productNumbers.Add(profile.ProductNum))
                throw new InvalidDataException(string.Format(T("ErrDuplicatePn"), profile.ProductNum));
            if (string.IsNullOrEmpty(profile.Name) || Encoding.UTF8.GetByteCount(profile.Name) > 23)
                throw new InvalidDataException(string.Format(T("ErrName"), context));
            if (profile.Fields.Count == 0 || profile.Fields.Count > MaxFields)
                throw new InvalidDataException(string.Format(T("ErrFieldCount"), context));

            HashSet<string> keys = new HashSet<string>(StringComparer.Ordinal);
            List<int[]> ranges = new List<int[]>();
            for (int f = 0; f < profile.Fields.Count; ++f)
            {
                ProfileField field = profile.Fields[f];
                string fieldContext = string.Format(T("FieldContext"), context, f + 1);
                field.Key = (field.Key ?? "").Trim();
                field.Label = (field.Label ?? "").Trim();
                field.ValueType = (field.ValueType ?? "").Trim();
                field.WordOrder = (field.WordOrder ?? "").Trim();
                if (field.Key.Length == 0 || Encoding.UTF8.GetByteCount(field.Key) > 23)
                    throw new InvalidDataException(string.Format(T("ErrKey"), fieldContext));
                if (!keys.Add(field.Key))
                    throw new InvalidDataException(string.Format(T("ErrDuplicateKey"), context, field.Key));
                if (field.Label.Length == 0 || Encoding.UTF8.GetByteCount(field.Label) > 23)
                    throw new InvalidDataException(string.Format(T("ErrLabel"), fieldContext));
                if (field.Function != 3 || field.AddressBase != 1)
                    throw new InvalidDataException(string.Format(T("ErrFcBase"), fieldContext));
                if (field.ValueType != "uint16" && field.ValueType != "float32")
                    throw new InvalidDataException(string.Format(T("ErrType"), fieldContext));
                int registerCount = field.ValueType == "float32" ? 2 : 1;
                if (field.Address < 1 || field.Address + registerCount - 1 > 65536)
                    throw new InvalidDataException(string.Format(T("ErrAddress"), fieldContext));
                if (field.ValueType == "float32" && field.WordOrder != "CDAB")
                    throw new InvalidDataException(string.Format(T("ErrWordOrder"), fieldContext));
                if (field.Decimals < 0 || field.Decimals > 3)
                    throw new InvalidDataException(string.Format(T("ErrDecimals"), fieldContext));
                ranges.Add(new int[] { field.Address, field.Address + registerCount - 1, f });
            }
            ranges.Sort(delegate(int[] a, int[] b) { return a[0].CompareTo(b[0]); });
            for (int r = 1; r < ranges.Count; ++r)
                if (ranges[r][0] <= ranges[r - 1][1])
                    throw new InvalidDataException(string.Format(T("ErrOverlap"), context));
        }
    }

    private string BuildJson()
    {
        JavaScriptSerializer serializer = new JavaScriptSerializer();
        StringBuilder json = new StringBuilder();
        json.AppendLine("{");
        json.AppendLine("  \"schema_version\": 1,");
        json.Append("  \"config_version\": ").Append(serializer.Serialize(versionText.Text.Trim())).AppendLine(",");
        json.AppendLine("  \"profiles\": [");
        for (int i = 0; i < profiles.Count; ++i)
        {
            SensorProfile profile = profiles[i];
            json.AppendLine("    {");
            json.Append("      \"product_num\": ").Append(profile.ProductNum).AppendLine(",");
            json.Append("      \"name\": ").Append(serializer.Serialize(profile.Name)).AppendLine(",");
            json.AppendLine("      \"fields\": [");
            for (int f = 0; f < profile.Fields.Count; ++f)
            {
                ProfileField field = profile.Fields[f];
                json.Append("        {\"key\": ").Append(serializer.Serialize(field.Key));
                json.Append(", \"label\": ").Append(serializer.Serialize(field.Label));
                json.Append(", \"function\": 3");
                json.Append(", \"address\": ").Append(field.Address);
                json.Append(", \"address_base\": 1");
                json.Append(", \"type\": ").Append(serializer.Serialize(field.ValueType));
                if (field.ValueType == "float32")
                    json.Append(", \"word_order\": \"CDAB\"");
                json.Append(", \"decimals\": ").Append(field.Decimals).Append("}");
                json.AppendLine(f + 1 < profile.Fields.Count ? "," : "");
            }
            json.AppendLine("      ]");
            json.Append("    }").AppendLine(i + 1 < profiles.Count ? "," : "");
        }
        json.AppendLine("  ]");
        json.AppendLine("}");
        return json.ToString();
    }

    private bool ConfirmDiscard()
    {
        if (!dirty)
            return true;
        DialogResult answer = MessageBox.Show(this, T("SaveBeforeContinuing"), Text,
            MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
        if (answer == DialogResult.Cancel) return false;
        if (answer == DialogResult.Yes) return SaveDocument(false);
        return true;
    }

    private void FormIsClosing(object sender, FormClosingEventArgs e)
    {
        if (!ConfirmDiscard())
            e.Cancel = true;
    }

    private void MarkDirty()
    {
        if (switchingProfile) return;
        dirty = true;
        UpdateCaption();
    }

    private void ApplyLanguage()
    {
        newToolButton.Text = T("New");
        openToolButton.Text = T("Open");
        saveToolButton.Text = T("Save");
        saveAsToolButton.Text = T("SaveAs");
        validateToolButton.Text = T("Validate");
        versionCaption.Text = T("ConfigVersion") + ":";
        languageCaption.Text = T("Language") + ":";
        supportedProductsCaption.Text = T("SupportedProducts");
        profileDetailsCaption.Text = T("ProfileDetails");
        productPnCaption.Text = T("ProductPN");
        nameCaption.Text = T("Name");
        addProfileButton.Text = T("Add");
        duplicateProfileButton.Text = T("Duplicate");
        deleteProfileButton.Text = T("Delete");
        addFieldButton.Text = T("AddField");
        deleteFieldButton.Text = T("DeleteField");
        fieldHintCaption.Text = T("FieldHint");
        if (fieldGrid.Columns.Count >= 8)
        {
            fieldGrid.Columns[0].HeaderText = T("Key");
            fieldGrid.Columns[1].HeaderText = T("Label");
            fieldGrid.Columns[2].HeaderText = "FC";
            fieldGrid.Columns[3].HeaderText = T("Address");
            fieldGrid.Columns[4].HeaderText = T("Base");
            fieldGrid.Columns[5].HeaderText = T("Type");
            fieldGrid.Columns[6].HeaderText = T("WordOrder");
            fieldGrid.Columns[7].HeaderText = T("Decimals");
        }
        UpdateCaption();
    }

    private string T(string key)
    {
        if (language == "zh")
        {
            switch (key)
            {
                case "WindowTitle": return "传感器配置管理器";
                case "New": return "新建";
                case "Open": return "打开";
                case "Save": return "保存";
                case "SaveAs": return "另存为";
                case "Validate": return "校验";
                case "ConfigVersion": return "配置版本";
                case "Language": return "语言";
                case "SupportedProducts": return "支持的设备";
                case "ProfileDetails": return "配置详情";
                case "ProductPN": return "产品 PN";
                case "Name": return "名称";
                case "Add": return "新增";
                case "Duplicate": return "复制";
                case "Delete": return "删除";
                case "AddField": return "新增参数";
                case "DeleteField": return "删除参数";
                case "FieldHint": return "最多 16 个参数；地址按 1 开始填写。";
                case "Key": return "键名";
                case "Label": return "显示名称";
                case "Address": return "地址";
                case "Base": return "基数";
                case "Type": return "类型";
                case "WordOrder": return "字序";
                case "Decimals": return "小数位";
                case "OpenFailed": return "打开失败";
                case "SaveFailed": return "保存失败";
                case "ValidationFailed": return "校验失败";
                case "ConfigurationValid": return "配置校验通过。";
                case "MaxProfiles": return "最多支持 50 个设备配置。";
                case "AtLeastOneProfile": return "至少需要保留一个设备配置。";
                case "DeletePn": return "确定删除 PN {0} 吗？";
                case "ConfirmDelete": return "确认删除";
                case "MaxFields": return "每个设备最多支持 16 个参数。";
                case "AtLeastOneField": return "至少需要保留一个参数。";
                case "SaveBeforeContinuing": return "继续之前是否保存修改？";
                case "NewConfiguration": return "新配置";
                case "LoadedStatus": return "已从 {1} 加载 {0} 个设备配置";
                case "ValidStatus": return "校验通过：{0} 个设备，{1} 个参数";
                case "SavedStatus": return "已保存 {0} 个设备、{1} 字节，并生成 {2}";
                case "JsonFilter": return "传感器配置 JSON (*.json)|*.json|所有文件 (*.*)|*.*";
                case "ErrJsonSize": return "JSON 文件不能超过 262144 字节。";
                case "ErrVersion": return "配置版本必须为 1～23 个 UTF-8 字节。";
                case "ErrProfileCount": return "设备配置数量必须为 1～50。";
                case "ErrPnRange": return "产品 PN 必须在 1～4294967295 范围内。";
                case "ErrDuplicatePn": return "产品 PN 重复：{0}";
                case "ErrName": return "{0}：名称必须为 1～23 个 UTF-8 字节。";
                case "ErrFieldCount": return "{0}：参数数量必须为 1～16。";
                case "FieldContext": return "{0}，第 {1} 个参数";
                case "ErrKey": return "{0}：键名必须为 1～23 个 UTF-8 字节。";
                case "ErrDuplicateKey": return "{0}：键名 {1} 重复。";
                case "ErrLabel": return "{0}：显示名称必须为 1～23 个 UTF-8 字节。";
                case "ErrFcBase": return "{0}：目前只支持 FC03 和地址基数 1。";
                case "ErrType": return "{0}：类型必须为 uint16 或 float32。";
                case "ErrAddress": return "{0}：地址超出 Modbus 范围。";
                case "ErrWordOrder": return "{0}：float32 字序必须为 CDAB。";
                case "ErrDecimals": return "{0}：小数位必须为 0～3。";
                case "ErrOverlap": return "{0}：寄存器地址存在重叠。";
            }
        }
        else if (language == "es")
        {
            switch (key)
            {
                case "WindowTitle": return "Gestor de perfiles de sensores";
                case "New": return "Nuevo";
                case "Open": return "Abrir";
                case "Save": return "Guardar";
                case "SaveAs": return "Guardar como";
                case "Validate": return "Validar";
                case "ConfigVersion": return "Versión";
                case "Language": return "Idioma";
                case "SupportedProducts": return "Dispositivos compatibles";
                case "ProfileDetails": return "Detalles del perfil";
                case "ProductPN": return "PN del producto";
                case "Name": return "Nombre";
                case "Add": return "Añadir";
                case "Duplicate": return "Duplicar";
                case "Delete": return "Eliminar";
                case "AddField": return "Añadir parámetro";
                case "DeleteField": return "Eliminar parámetro";
                case "FieldHint": return "Máximo 16 parámetros; las direcciones empiezan en 1.";
                case "Key": return "Clave";
                case "Label": return "Etiqueta";
                case "Address": return "Dirección";
                case "Base": return "Base";
                case "Type": return "Tipo";
                case "WordOrder": return "Orden";
                case "Decimals": return "Decimales";
                case "OpenFailed": return "Error al abrir";
                case "SaveFailed": return "Error al guardar";
                case "ValidationFailed": return "Error de validación";
                case "ConfigurationValid": return "La configuración es válida.";
                case "MaxProfiles": return "Se admiten como máximo 50 perfiles.";
                case "AtLeastOneProfile": return "Debe conservar al menos un perfil.";
                case "DeletePn": return "¿Eliminar el PN {0}?";
                case "ConfirmDelete": return "Confirmar eliminación";
                case "MaxFields": return "Cada perfil admite como máximo 16 parámetros.";
                case "AtLeastOneField": return "Debe conservar al menos un parámetro.";
                case "SaveBeforeContinuing": return "¿Guardar los cambios antes de continuar?";
                case "NewConfiguration": return "Nueva configuración";
                case "LoadedStatus": return "Se cargaron {0} perfiles desde {1}";
                case "ValidStatus": return "Válido: {0} perfiles, {1} parámetros";
                case "SavedStatus": return "Guardados {0} perfiles, {1} bytes y generado {2}";
                case "JsonFilter": return "Perfiles JSON (*.json)|*.json|Todos los archivos (*.*)|*.*";
                case "ErrJsonSize": return "El archivo JSON no puede superar 262144 bytes.";
                case "ErrVersion": return "La versión debe contener entre 1 y 23 bytes UTF-8.";
                case "ErrProfileCount": return "El número de perfiles debe estar entre 1 y 50.";
                case "ErrPnRange": return "El PN debe estar entre 1 y 4294967295.";
                case "ErrDuplicatePn": return "PN duplicado: {0}";
                case "ErrName": return "{0}: el nombre debe contener entre 1 y 23 bytes UTF-8.";
                case "ErrFieldCount": return "{0}: debe contener entre 1 y 16 parámetros.";
                case "FieldContext": return "{0}, parámetro {1}";
                case "ErrKey": return "{0}: la clave debe contener entre 1 y 23 bytes UTF-8.";
                case "ErrDuplicateKey": return "{0}: clave duplicada {1}.";
                case "ErrLabel": return "{0}: la etiqueta debe contener entre 1 y 23 bytes UTF-8.";
                case "ErrFcBase": return "{0}: solo se admiten FC03 y base de dirección 1.";
                case "ErrType": return "{0}: el tipo debe ser uint16 o float32.";
                case "ErrAddress": return "{0}: la dirección está fuera del rango Modbus.";
                case "ErrWordOrder": return "{0}: el orden de float32 debe ser CDAB.";
                case "ErrDecimals": return "{0}: los decimales deben estar entre 0 y 3.";
                case "ErrOverlap": return "{0}: los campos de registros se superponen.";
            }
        }
        else
        {
            switch (key)
            {
                case "WindowTitle": return "Sensor Profile Manager";
                case "New": return "New";
                case "Open": return "Open";
                case "Save": return "Save";
                case "SaveAs": return "Save As";
                case "Validate": return "Validate";
                case "ConfigVersion": return "Config version";
                case "Language": return "Language";
                case "SupportedProducts": return "Supported products";
                case "ProfileDetails": return "Profile details";
                case "ProductPN": return "Product PN";
                case "Name": return "Name";
                case "Add": return "Add";
                case "Duplicate": return "Duplicate";
                case "Delete": return "Delete";
                case "AddField": return "Add field";
                case "DeleteField": return "Delete field";
                case "FieldHint": return "Up to 16 fields; addresses shown here are 1-based.";
                case "Key": return "Key";
                case "Label": return "Label";
                case "Address": return "Address";
                case "Base": return "Base";
                case "Type": return "Type";
                case "WordOrder": return "Word order";
                case "Decimals": return "Decimals";
                case "OpenFailed": return "Open failed";
                case "SaveFailed": return "Save failed";
                case "ValidationFailed": return "Validation failed";
                case "ConfigurationValid": return "Configuration is valid.";
                case "MaxProfiles": return "A maximum of 50 profiles is supported.";
                case "AtLeastOneProfile": return "At least one profile is required.";
                case "DeletePn": return "Delete PN {0}?";
                case "ConfirmDelete": return "Confirm delete";
                case "MaxFields": return "A maximum of 16 fields is supported per profile.";
                case "AtLeastOneField": return "At least one field is required.";
                case "SaveBeforeContinuing": return "Save changes before continuing?";
                case "NewConfiguration": return "New configuration";
                case "LoadedStatus": return "Loaded {0} profiles from {1}";
                case "ValidStatus": return "Valid: {0} profiles, {1} fields";
                case "SavedStatus": return "Saved {0} profiles, {1} bytes, and generated {2}";
                case "JsonFilter": return "Sensor profile JSON (*.json)|*.json|All files (*.*)|*.*";
                case "ErrJsonSize": return "JSON cannot exceed 262144 bytes.";
                case "ErrVersion": return "Config version must contain 1..23 UTF-8 bytes.";
                case "ErrProfileCount": return "Profile count must be 1..50.";
                case "ErrPnRange": return "Product PN must be 1..4294967295.";
                case "ErrDuplicatePn": return "Duplicate product PN: {0}";
                case "ErrName": return "{0}: name must contain 1..23 UTF-8 bytes.";
                case "ErrFieldCount": return "{0}: field count must be 1..16.";
                case "FieldContext": return "{0}, field {1}";
                case "ErrKey": return "{0}: key must contain 1..23 UTF-8 bytes.";
                case "ErrDuplicateKey": return "{0}: duplicate key {1}.";
                case "ErrLabel": return "{0}: label must contain 1..23 UTF-8 bytes.";
                case "ErrFcBase": return "{0}: only FC03 and address base 1 are supported.";
                case "ErrType": return "{0}: type must be uint16 or float32.";
                case "ErrAddress": return "{0}: address is outside the Modbus range.";
                case "ErrWordOrder": return "{0}: float32 word order must be CDAB.";
                case "ErrDecimals": return "{0}: decimals must be 0..3.";
                case "ErrOverlap": return "{0}: register fields overlap.";
            }
        }
        return key;
    }

    private void UpdateCaption()
    {
        Text = T("WindowTitle") + (dirty ? " *" : "") +
            (string.IsNullOrEmpty(currentPath) ? "" : " - " + Path.GetFileName(currentPath));
    }

    private void SetStatus(string text)
    {
        statusLabel.Text = text;
    }
}
