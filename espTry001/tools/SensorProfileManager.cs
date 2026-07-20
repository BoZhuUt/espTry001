using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
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
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new ProfileManagerForm(args.Length > 0 ? args[0] : null));
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
    private const int MaxProfiles = 32;
    private const int MaxFields = 16;
    private const int MaxJsonBytes = 64 * 1024;

    private readonly List<SensorProfile> profiles = new List<SensorProfile>();
    private readonly ListBox profileList = new ListBox();
    private readonly TextBox productNumText = new TextBox();
    private readonly TextBox nameText = new TextBox();
    private readonly TextBox versionText = new TextBox();
    private readonly DataGridView fieldGrid = new DataGridView();
    private readonly ToolStripStatusLabel statusLabel = new ToolStripStatusLabel();
    private string currentPath;
    private int currentProfileIndex = -1;
    private bool switchingProfile;
    private bool dirty;

    internal ProfileManagerForm(string initialPath)
    {
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
        tool.Items.Add(MakeToolButton("New", delegate { NewRequested(); }));
        tool.Items.Add(MakeToolButton("Open", delegate { OpenRequested(); }));
        tool.Items.Add(MakeToolButton("Save", delegate { SaveDocument(false); }));
        tool.Items.Add(MakeToolButton("Save As", delegate { SaveDocument(true); }));
        tool.Items.Add(new ToolStripSeparator());
        tool.Items.Add(MakeToolButton("Validate", delegate { ValidateRequested(); }));
        tool.Items.Add(new ToolStripSeparator());
        tool.Items.Add(new ToolStripLabel("Config version:"));
        ToolStripControlHost versionHost = new ToolStripControlHost(versionText);
        versionText.Width = 145;
        versionText.TextChanged += delegate { if (!switchingProfile) MarkDirty(); };
        tool.Items.Add(versionHost);
        return tool;
    }

    private static ToolStripButton MakeToolButton(string text, EventHandler click)
    {
        ToolStripButton button = new ToolStripButton(text);
        button.DisplayStyle = ToolStripItemDisplayStyle.Text;
        button.Click += click;
        return button;
    }

    private void BuildProfilePanel(Control panel)
    {
        Label title = new Label();
        title.Text = "Supported products";
        title.Font = new Font(Font, FontStyle.Bold);
        title.Dock = DockStyle.Top;
        title.Height = 28;
        panel.Controls.Add(title);

        FlowLayoutPanel buttons = new FlowLayoutPanel();
        buttons.Dock = DockStyle.Bottom;
        buttons.Height = 42;
        buttons.FlowDirection = FlowDirection.LeftToRight;
        buttons.WrapContents = false;
        buttons.Padding = new Padding(0, 4, 0, 0);
        buttons.Controls.Add(MakeButton("Add", AddProfile, 58));
        buttons.Controls.Add(MakeButton("Duplicate", DuplicateProfile, 82));
        buttons.Controls.Add(MakeButton("Delete", DeleteProfile, 62));
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

        Label heading = new Label();
        heading.Text = "Profile details";
        heading.Font = new Font("Segoe UI", 13F, FontStyle.Bold);
        heading.Dock = DockStyle.Fill;
        heading.TextAlign = ContentAlignment.MiddleLeft;
        layout.Controls.Add(heading, 0, 0);
        layout.SetColumnSpan(heading, 4);

        layout.Controls.Add(MakeFieldLabel("Product PN"), 0, 1);
        productNumText.Dock = DockStyle.Fill;
        productNumText.TextChanged += HeaderChanged;
        layout.Controls.Add(productNumText, 1, 1);
        layout.Controls.Add(MakeFieldLabel("Name"), 2, 1);
        nameText.Dock = DockStyle.Fill;
        nameText.TextChanged += HeaderChanged;
        layout.Controls.Add(nameText, 3, 1);

        FlowLayoutPanel fieldButtons = new FlowLayoutPanel();
        fieldButtons.Dock = DockStyle.Fill;
        fieldButtons.Controls.Add(MakeButton("Add field", AddField, 90));
        fieldButtons.Controls.Add(MakeButton("Delete field", DeleteField, 100));
        Label hint = new Label();
        hint.Text = "Up to 16 fields; addresses shown here are 1-based.";
        hint.AutoSize = true;
        hint.Margin = new Padding(14, 8, 0, 0);
        fieldButtons.Controls.Add(hint);
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

    private static Label MakeFieldLabel(string text)
    {
        Label label = new Label();
        label.Text = text;
        label.Dock = DockStyle.Fill;
        label.TextAlign = ContentAlignment.MiddleLeft;
        return label;
    }

    private static Button MakeButton(string text, EventHandler click, int width)
    {
        Button button = new Button();
        button.Text = text;
        button.Width = width;
        button.Height = 30;
        button.Click += click;
        return button;
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
            SetStatus("Loaded " + loaded.Count + " profiles from " + currentPath);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Open failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static List<SensorProfile> ParseProfiles(IDictionary<string, object> root)
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
        SetStatus("New configuration");
    }

    private void OpenRequested()
    {
        if (!ConfirmDiscard())
            return;
        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Filter = "Sensor profile JSON (*.json)|*.json|All files (*.*)|*.*";
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
                    dialog.Filter = "Sensor profile JSON (*.json)|*.json|All files (*.*)|*.*";
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
                throw new InvalidDataException("JSON exceeds 65536 bytes.");
            File.WriteAllBytes(currentPath, bytes);
            dirty = false;
            UpdateCaption();
            SetStatus("Saved " + profiles.Count + " profiles, " + bytes.Length + " bytes");
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Save failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
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
            SetStatus(string.Format("Valid: {0} profiles, {1} fields", profiles.Count, fields));
            MessageBox.Show(this, "Configuration is valid.", Text,
                MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Validation failed",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void AddProfile(object sender, EventArgs e)
    {
        CommitCurrentProfile();
        if (profiles.Count >= MaxProfiles)
        {
            MessageBox.Show(this, "A maximum of 32 profiles is supported.");
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
            MessageBox.Show(this, "At least one profile is required.");
            return;
        }
        SensorProfile target = profiles[currentProfileIndex];
        if (MessageBox.Show(this, "Delete PN " + target.ProductNum + "?", "Confirm delete",
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
            MessageBox.Show(this, "A maximum of 16 fields is supported per profile.");
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
            MessageBox.Show(this, "At least one field is required.");
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
            throw new InvalidDataException("Config version must contain 1..23 UTF-8 bytes.");
        if (profiles.Count == 0 || profiles.Count > MaxProfiles)
            throw new InvalidDataException("Profile count must be 1..32.");

        HashSet<uint> productNumbers = new HashSet<uint>();
        for (int i = 0; i < profiles.Count; ++i)
        {
            SensorProfile profile = profiles[i];
            string context = "PN " + profile.ProductNum;
            if (profile.ProductNum == 0)
                throw new InvalidDataException("Product PN must be 1..4294967295.");
            if (!productNumbers.Add(profile.ProductNum))
                throw new InvalidDataException("Duplicate product PN: " + profile.ProductNum);
            if (string.IsNullOrEmpty(profile.Name) || Encoding.UTF8.GetByteCount(profile.Name) > 23)
                throw new InvalidDataException(context + ": name must contain 1..23 UTF-8 bytes.");
            if (profile.Fields.Count == 0 || profile.Fields.Count > MaxFields)
                throw new InvalidDataException(context + ": field count must be 1..16.");

            HashSet<string> keys = new HashSet<string>(StringComparer.Ordinal);
            List<int[]> ranges = new List<int[]>();
            for (int f = 0; f < profile.Fields.Count; ++f)
            {
                ProfileField field = profile.Fields[f];
                string fieldContext = context + ", field " + (f + 1);
                field.Key = (field.Key ?? "").Trim();
                field.Label = (field.Label ?? "").Trim();
                field.ValueType = (field.ValueType ?? "").Trim();
                field.WordOrder = (field.WordOrder ?? "").Trim();
                if (field.Key.Length == 0 || Encoding.UTF8.GetByteCount(field.Key) > 23)
                    throw new InvalidDataException(fieldContext + ": key must contain 1..23 UTF-8 bytes.");
                if (!keys.Add(field.Key))
                    throw new InvalidDataException(context + ": duplicate key " + field.Key);
                if (field.Label.Length == 0 || Encoding.UTF8.GetByteCount(field.Label) > 23)
                    throw new InvalidDataException(fieldContext + ": label must contain 1..23 UTF-8 bytes.");
                if (field.Function != 3 || field.AddressBase != 1)
                    throw new InvalidDataException(fieldContext + ": only FC03 and address base 1 are supported.");
                if (field.ValueType != "uint16" && field.ValueType != "float32")
                    throw new InvalidDataException(fieldContext + ": type must be uint16 or float32.");
                int registerCount = field.ValueType == "float32" ? 2 : 1;
                if (field.Address < 1 || field.Address + registerCount - 1 > 65536)
                    throw new InvalidDataException(fieldContext + ": address is outside Modbus range.");
                if (field.ValueType == "float32" && field.WordOrder != "CDAB")
                    throw new InvalidDataException(fieldContext + ": float32 word order must be CDAB.");
                if (field.Decimals < 0 || field.Decimals > 3)
                    throw new InvalidDataException(fieldContext + ": decimals must be 0..3.");
                ranges.Add(new int[] { field.Address, field.Address + registerCount - 1, f });
            }
            ranges.Sort(delegate(int[] a, int[] b) { return a[0].CompareTo(b[0]); });
            for (int r = 1; r < ranges.Count; ++r)
                if (ranges[r][0] <= ranges[r - 1][1])
                    throw new InvalidDataException(context + ": register fields overlap.");
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
        DialogResult answer = MessageBox.Show(this, "Save changes before continuing?", Text,
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

    private void UpdateCaption()
    {
        Text = "Sensor Profile Manager" + (dirty ? " *" : "") +
            (string.IsNullOrEmpty(currentPath) ? "" : " - " + Path.GetFileName(currentPath));
    }

    private void SetStatus(string text)
    {
        statusLabel.Text = text;
    }
}
