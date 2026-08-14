using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Diagnostics;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class CalibrationProfileManagerProgram
{
    [STAThread]
    private static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new CalibrationProfileManagerForm(args.Length > 0 ? args[0] : null));
    }
}

internal sealed class CalibrationProfileEntry
{
    internal string Path;
    internal IDictionary<string, object> Root;

    public override string ToString()
    {
        object pn;
        object model;
        object status;
        Root.TryGetValue("product_num", out pn);
        Root.TryGetValue("model_name", out model);
        Root.TryGetValue("release_status", out status);
        return Convert.ToString(pn) + "  " + Convert.ToString(model) +
            "  [" + Convert.ToString(status) + "]";
    }
}

internal sealed class JsonNodeBinding
{
    internal string Kind;
    internal IDictionary<string, object> Value;
    internal IList Parent;
    internal int Index;
    internal IDictionary<string, object> Procedure;
}

internal sealed class ResultRowBinding
{
    internal IDictionary<string, object> Mapping;
    internal IList Parent;
}

internal sealed class ReviewOperationItem
{
    internal IDictionary<string, object> Procedure;
    internal int Number;
    internal bool Confirmed;

    public override string ToString()
    {
        string category = Convert.ToString(CalibrationProfileManagerForm.Value(Procedure, "category"));
        string reading = Convert.ToString(CalibrationProfileManagerForm.Value(Procedure, "reading_name"));
        string name = Convert.ToString(CalibrationProfileManagerForm.Value(Procedure, "name"));
        return (Confirmed ? "[OK] " : "[  ] ") + Number + ". " +
            (category == "output_span" ? "4-20mA SET" : name) +
            "  (" + reading + ")";
    }
}

internal sealed class CalibrationProfileManagerForm : Form
{
    private readonly JavaScriptSerializer serializer = new JavaScriptSerializer();
    private readonly ListBox profileList = new ListBox();
    private readonly TreeView procedureTree = new TreeView();
    private readonly DataGridView resultGrid = new DataGridView();
    private readonly ListBox operationList = new ListBox();
    private readonly TextBox operationSummary = new TextBox();
    private readonly Label reviewProgressLabel = new Label();
    private readonly Button confirmOperationButton = new Button();
    private readonly CheckedListBox reviewChecks = new CheckedListBox();
    private readonly TextBox blockerText = new TextBox();
    private readonly Label folderLabel = new Label();
    private readonly Label profileLabel = new Label();
    private readonly Label statusLabel = new Label();
    private readonly ComboBox releaseStatus = new ComboBox();
    private readonly Button saveButton = new Button();
    private readonly List<CalibrationProfileEntry> profiles = new List<CalibrationProfileEntry>();
    private string currentFolder;
    private CalibrationProfileEntry current;
    private bool dirty;
    private bool loading;

    private const int ReviewInputRanges = 0;
    private const int ReviewResultMappings = 1;
    private const int ReviewVersionConditions = 2;
    private const int ReviewCommandSequences = 3;

    internal CalibrationProfileManagerForm(string initialPath)
    {
        serializer.MaxJsonLength = 16 * 1024 * 1024;
        Text = "Calibration Profile Manager";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(1050, 650);
        Size = new Size(1320, 800);
        Font = new Font("Segoe UI", 9F);
        BuildUi();

        string path = initialPath;
        if (String.IsNullOrWhiteSpace(path))
            path = DefaultCalibrationFolder();
        // BeginInvoke cannot be used until WinForms has created the native
        // window handle. Loading on Shown keeps startup deterministic when the
        // executable is launched directly from Explorer.
        Shown += delegate { OpenInitialPath(path); };
    }

    private void BuildUi()
    {
        TableLayoutPanel shell = new TableLayoutPanel();
        shell.Dock = DockStyle.Fill;
        shell.ColumnCount = 1;
        shell.RowCount = 3;
        shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 27));
        shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        Controls.Add(shell);

        ToolStrip toolbar = new ToolStrip();
        ToolStripButton open = new ToolStripButton("Open Folder");
        ToolStripButton import = new ToolStripButton("Import APP JSON");
        ToolStripButton reload = new ToolStripButton("Reload");
        ToolStripButton validate = new ToolStripButton("Validate");
        ToolStripButton approve = new ToolStripButton("Approve Profile");
        ToolStripButton save = new ToolStripButton("Save");
        ToolStripButton publish = new ToolStripButton("Publish to Device");
        ToolStripButton addOperation = new ToolStripButton("+ Operation");
        ToolStripButton addInput = new ToolStripButton("+ Input");
        ToolStripButton addStep = new ToolStripButton("+ Step");
        ToolStripButton editJson = new ToolStripButton("Edit JSON");
        ToolStripButton deleteNode = new ToolStripButton("Delete");
        ToolStripButton moveUp = new ToolStripButton("Up");
        ToolStripButton moveDown = new ToolStripButton("Down");
        ToolStripButton addResult = new ToolStripButton("+ Result");
        ToolStripButton deleteResult = new ToolStripButton("- Result");
        open.Click += delegate { ChooseFolder(); };
        import.Click += delegate { ImportAppJson(); };
        reload.Click += delegate { ReloadFolder(); };
        validate.Click += delegate { ValidateAndReport(); };
        approve.Click += delegate { ApproveCurrent(); };
        save.Click += delegate { SaveCurrent(); };
        publish.Click += delegate { PublishToDevice(); };
        addOperation.Click += delegate { AddOperation(); };
        addInput.Click += delegate { AddInput(); };
        addStep.Click += delegate { AddStep(); };
        editJson.Click += delegate { EditSelectedJson(); };
        deleteNode.Click += delegate { DeleteSelectedNode(); };
        moveUp.Click += delegate { MoveSelectedNode(-1); };
        moveDown.Click += delegate { MoveSelectedNode(1); };
        addResult.Click += delegate { AddResultMapping(); };
        deleteResult.Click += delegate { DeleteResultMapping(); };
        toolbar.Items.Add(open);
        toolbar.Items.Add(import);
        toolbar.Items.Add(reload);
        toolbar.Items.Add(new ToolStripSeparator());
        toolbar.Items.Add(validate);
        toolbar.Items.Add(approve);
        toolbar.Items.Add(save);
        toolbar.Items.Add(publish);
        toolbar.Items.Add(new ToolStripSeparator());
        toolbar.Items.Add(addOperation);
        toolbar.Items.Add(addInput);
        toolbar.Items.Add(addStep);
        toolbar.Items.Add(editJson);
        toolbar.Items.Add(deleteNode);
        toolbar.Items.Add(moveUp);
        toolbar.Items.Add(moveDown);
        toolbar.Items.Add(new ToolStripSeparator());
        toolbar.Items.Add(addResult);
        toolbar.Items.Add(deleteResult);
        toolbar.Dock = DockStyle.Fill;
        shell.Controls.Add(toolbar, 0, 0);

        folderLabel.Dock = DockStyle.Fill;
        folderLabel.Padding = new Padding(8, 6, 8, 0);
        folderLabel.ForeColor = Color.DimGray;
        shell.Controls.Add(folderLabel, 0, 1);

        SplitContainer outer = new SplitContainer();
        outer.Dock = DockStyle.Fill;
        outer.SplitterDistance = 285;
        outer.Panel1.Padding = new Padding(6);
        outer.Panel2.Padding = new Padding(6);
        shell.Controls.Add(outer, 0, 2);

        Label profilesTitle = new Label();
        profilesTitle.Text = "Normalized profiles";
        profilesTitle.Dock = DockStyle.Top;
        profilesTitle.Height = 28;
        profilesTitle.Font = new Font(Font, FontStyle.Bold);
        profileList.Dock = DockStyle.Fill;
        profileList.IntegralHeight = false;
        profileList.SelectedIndexChanged += ProfileSelectionChanged;
        outer.Panel1.Controls.Add(profileList);
        outer.Panel1.Controls.Add(profilesTitle);

        TabControl editorTabs = new TabControl();
        editorTabs.Dock = DockStyle.Fill;
        TabPage reviewPage = new TabPage("Review");
        TabPage advancedPage = new TabPage("Advanced");
        editorTabs.TabPages.Add(reviewPage);
        editorTabs.TabPages.Add(advancedPage);
        outer.Panel2.Controls.Add(editorTabs);
        BuildReviewPage(reviewPage);

        TableLayoutPanel right = new TableLayoutPanel();
        right.Dock = DockStyle.Fill;
        right.ColumnCount = 1;
        right.RowCount = 5;
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 42));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 58));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 122));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        advancedPage.Controls.Add(right);

        Panel header = new Panel();
        header.Dock = DockStyle.Fill;
        profileLabel.AutoSize = true;
        profileLabel.Location = new Point(4, 12);
        profileLabel.Font = new Font(Font, FontStyle.Bold);
        Label releaseLabel = new Label();
        releaseLabel.Text = "Release status:";
        releaseLabel.AutoSize = true;
        releaseLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        releaseLabel.Location = new Point(650, 12);
        releaseStatus.DropDownStyle = ComboBoxStyle.DropDownList;
        releaseStatus.Items.AddRange(new object[] { "approved", "draft", "blocked" });
        releaseStatus.Width = 115;
        releaseStatus.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        releaseStatus.Location = new Point(750, 8);
        releaseStatus.SelectedIndexChanged += ReleaseStatusChanged;
        saveButton.Text = "Save";
        saveButton.Width = 80;
        saveButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        saveButton.Location = new Point(875, 7);
        saveButton.Click += delegate { SaveCurrent(); };
        header.Resize += delegate
        {
            saveButton.Left = header.ClientSize.Width - saveButton.Width - 4;
            releaseStatus.Left = saveButton.Left - releaseStatus.Width - 8;
            releaseLabel.Left = releaseStatus.Left - releaseLabel.Width - 8;
        };
        header.Controls.Add(profileLabel);
        header.Controls.Add(releaseLabel);
        header.Controls.Add(releaseStatus);
        header.Controls.Add(saveButton);
        right.Controls.Add(header, 0, 0);

        procedureTree.Dock = DockStyle.Fill;
        procedureTree.HideSelection = false;
        right.Controls.Add(procedureTree, 0, 1);

        ConfigureResultGrid();
        right.Controls.Add(resultGrid, 0, 2);

        SplitContainer review = new SplitContainer();
        review.Dock = DockStyle.Fill;
        review.SplitterDistance = 330;
        review.Panel1.Padding = new Padding(3);
        review.Panel2.Padding = new Padding(3);
        reviewChecks.Dock = DockStyle.Fill;
        reviewChecks.CheckOnClick = true;
        reviewChecks.Items.Add("Input ranges and value types reviewed");
        reviewChecks.Items.Add("Result mappings and outcomes reviewed");
        reviewChecks.Items.Add("Version conditions reviewed");
        reviewChecks.Items.Add("Command addresses, values, and waits reviewed");
        reviewChecks.ItemCheck += ReviewItemCheck;
        blockerText.Dock = DockStyle.Fill;
        blockerText.Multiline = true;
        blockerText.ReadOnly = true;
        blockerText.ScrollBars = ScrollBars.Vertical;
        blockerText.BackColor = Color.White;
        blockerText.ForeColor = Color.DarkRed;
        review.Panel1.Controls.Add(reviewChecks);
        review.Panel2.Controls.Add(blockerText);
        right.Controls.Add(review, 0, 3);

        statusLabel.Dock = DockStyle.Fill;
        statusLabel.Padding = new Padding(4, 5, 4, 0);
        statusLabel.ForeColor = Color.DimGray;
        right.Controls.Add(statusLabel, 0, 4);

        FormClosing += OnFormClosing;
    }

    private void ConfigureResultGrid()
    {
        resultGrid.Dock = DockStyle.Fill;
        resultGrid.AllowUserToAddRows = false;
        resultGrid.AllowUserToDeleteRows = false;
        resultGrid.RowHeadersVisible = false;
        resultGrid.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells;
        resultGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        resultGrid.MultiSelect = false;
        resultGrid.Columns.Add(ReadOnlyTextColumn("Procedure", 175));
        resultGrid.Columns.Add(ReadOnlyTextColumn("Step", 85));
        resultGrid.Columns.Add(ReadOnlyTextColumn("Address", 75));
        resultGrid.Columns.Add(EditableTextColumn("Value", 65));
        DataGridViewComboBoxColumn outcome = new DataGridViewComboBoxColumn();
        outcome.HeaderText = "Outcome";
        outcome.Width = 95;
        outcome.Items.AddRange("success", "failure", "pending", "info");
        resultGrid.Columns.Add(outcome);
        DataGridViewTextBoxColumn message = EditableTextColumn("Message", 330);
        message.AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
        message.DefaultCellStyle.WrapMode = DataGridViewTriState.True;
        resultGrid.Columns.Add(message);
        resultGrid.CellValueChanged += ResultCellValueChanged;
        resultGrid.CurrentCellDirtyStateChanged += delegate
        {
            if (resultGrid.IsCurrentCellDirty)
                resultGrid.CommitEdit(DataGridViewDataErrorContexts.Commit);
        };
        resultGrid.DataError += delegate(object sender, DataGridViewDataErrorEventArgs e)
        {
            e.ThrowException = false;
        };
    }

    private void BuildReviewPage(Control parent)
    {
        TableLayoutPanel layout = new TableLayoutPanel();
        layout.Dock = DockStyle.Fill;
        layout.ColumnCount = 2;
        layout.RowCount = 3;
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 65));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        parent.Controls.Add(layout);

        Label title = new Label();
        title.Text = "Select an operation and confirm its imported settings";
        title.Dock = DockStyle.Fill;
        title.Padding = new Padding(8, 12, 4, 0);
        title.Font = new Font(Font, FontStyle.Bold);
        layout.Controls.Add(title, 0, 0);
        layout.SetColumnSpan(title, 2);

        operationList.Dock = DockStyle.Fill;
        operationList.IntegralHeight = false;
        operationList.Font = new Font("Segoe UI", 10F);
        operationList.SelectedIndexChanged += delegate { ShowSelectedOperationSummary(); };
        layout.Controls.Add(operationList, 0, 1);

        operationSummary.Dock = DockStyle.Fill;
        operationSummary.Multiline = true;
        operationSummary.ReadOnly = true;
        operationSummary.ScrollBars = ScrollBars.Vertical;
        operationSummary.BackColor = Color.White;
        operationSummary.Font = new Font("Consolas", 10F);
        layout.Controls.Add(operationSummary, 1, 1);

        reviewProgressLabel.Dock = DockStyle.Fill;
        reviewProgressLabel.Padding = new Padding(8, 17, 4, 0);
        reviewProgressLabel.ForeColor = Color.DimGray;
        layout.Controls.Add(reviewProgressLabel, 0, 2);

        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Dock = DockStyle.Fill;
        actions.FlowDirection = FlowDirection.RightToLeft;
        actions.Padding = new Padding(4, 9, 4, 0);
        confirmOperationButton.Text = "Confirm operation";
        confirmOperationButton.AutoSize = true;
        confirmOperationButton.Click += delegate { ConfirmSelectedOperation(); };
        Button edit = new Button();
        edit.Text = "Edit JSON";
        edit.AutoSize = true;
        edit.Click += delegate { EditSelectedReviewOperation(); };
        Button deleteOperation = new Button();
        deleteOperation.Text = "Delete operation";
        deleteOperation.AutoSize = true;
        deleteOperation.Click += delegate { DeleteSelectedReviewOperation(); };
        Button approve = new Button();
        approve.Text = "Approve && Save";
        approve.AutoSize = true;
        approve.Click += delegate { ApproveAndSave(); };
        actions.Controls.Add(approve);
        actions.Controls.Add(confirmOperationButton);
        actions.Controls.Add(deleteOperation);
        actions.Controls.Add(edit);
        layout.Controls.Add(actions, 1, 2);
    }

    private static DataGridViewTextBoxColumn ReadOnlyTextColumn(string title, int width)
    {
        DataGridViewTextBoxColumn column = new DataGridViewTextBoxColumn();
        column.HeaderText = title;
        column.Width = width;
        column.ReadOnly = true;
        return column;
    }

    private static DataGridViewTextBoxColumn EditableTextColumn(string title, int width)
    {
        DataGridViewTextBoxColumn column = new DataGridViewTextBoxColumn();
        column.HeaderText = title;
        column.Width = width;
        return column;
    }

    private void OpenInitialPath(string path)
    {
        if (File.Exists(path))
            LoadFolder(Path.GetDirectoryName(Path.GetFullPath(path)), Path.GetFullPath(path));
        else
            LoadFolder(Path.GetFullPath(path), null);
    }

    private void ChooseFolder()
    {
        if (!ConfirmDiscardOrSave()) return;
        using (FolderBrowserDialog dialog = new FolderBrowserDialog())
        {
            dialog.Description = "Select normalized calibration profile folder";
            dialog.SelectedPath = currentFolder ?? "";
            if (dialog.ShowDialog(this) == DialogResult.OK)
                LoadFolder(dialog.SelectedPath, null);
        }
    }

    private void ImportAppJson()
    {
        if (!ConfirmDiscardOrSave()) return;
        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Title = "Select mobile APP device JSON";
            dialog.Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*";
            dialog.Multiselect = false;
            if (dialog.ShowDialog(this) != DialogResult.OK) return;

            string projectDir = WorkingDirectory();
            string importer = Path.Combine(ToolDirectory(),
                "import_calibration_profiles.py");
            string rules = ResolveDataFile("calibration_import_rules.json");
            string output = currentFolder;
            if (String.IsNullOrEmpty(output))
                output = DefaultCalibrationFolder();
            if (!File.Exists(importer) || !File.Exists(rules))
            {
                MessageBox.Show(this,
                    "Importer or project rules were not found.\nRun this tool from the project tools folder.",
                    "Import unavailable", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            int productNum;
            string profileId;
            try
            {
                IDictionary<string, object> source = serializer.DeserializeObject(
                    File.ReadAllText(dialog.FileName, Encoding.UTF8))
                    as IDictionary<string, object>;
                if (source == null ||
                    !Int32.TryParse(Convert.ToString(Get(source, "PN")), out productNum) ||
                    productNum <= 0)
                    throw new InvalidDataException("APP JSON does not contain a valid PN");
                string stem = Path.GetFileNameWithoutExtension(dialog.FileName);
                string suffix = stem.Contains("_") ? stem.Substring(stem.IndexOf('_') + 1) : "default";
                profileId = productNum + "." + SafeProfileId(suffix, "default");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Invalid APP JSON",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            string outputPath = Path.Combine(output, profileId + ".json");
            bool overwrite = File.Exists(outputPath);
            if (overwrite)
            {
                DialogResult existingChoice = MessageBox.Show(this,
                    "A normalized profile already exists:\n" + outputPath +
                    "\n\nYes: open the existing profile without changing it." +
                    "\nNo: replace it with a fresh conversion." +
                    "\nCancel: stop importing.",
                    "Profile already exists", MessageBoxButtons.YesNoCancel,
                    MessageBoxIcon.Question);
                if (existingChoice == DialogResult.Cancel) return;
                if (existingChoice == DialogResult.Yes)
                {
                    LoadFolder(output, Path.GetFullPath(outputPath));
                    return;
                }
            }

            string reportBase = Path.Combine(Path.GetTempPath(),
                "calibration_import_" + Guid.NewGuid().ToString("N"));
            List<string> arguments = new List<string>();
            arguments.Add(Quote(importer));
            arguments.Add("--rules " + Quote(rules));
            arguments.Add("--source-file " + Quote(dialog.FileName));
            arguments.Add("--output " + Quote(output));
            arguments.Add("--report-base " + Quote(reportBase));
            arguments.Add("--write");
            if (overwrite) arguments.Add("--overwrite");
            string stdout;
            string stderr;
            int exitCode;
            try
            {
                exitCode = RunImporter(projectDir, String.Join(" ", arguments.ToArray()),
                    out stdout, out stderr);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Import failed",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            if (exitCode != 0 || !File.Exists(outputPath))
            {
                MessageBox.Show(this,
                    "Conversion did not create a reviewable profile.\n\n" +
                    (String.IsNullOrWhiteSpace(stderr) ? stdout : stderr),
                    "Import blocked", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            LoadFolder(output, Path.GetFullPath(outputPath));
            MessageBox.Show(this,
                "APP JSON converted to a draft profile.\nReview each operation before approval.",
                "Import complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private static int RunImporter(string projectDir, string arguments,
                                   out string stdout, out string stderr)
    {
        string userProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        string python = Path.Combine(userProfile, ".platformio", "penv", "Scripts", "python.exe");
        if (!File.Exists(python)) python = "python";
        ProcessStartInfo start = new ProcessStartInfo();
        start.FileName = python;
        start.Arguments = arguments;
        start.WorkingDirectory = projectDir;
        start.UseShellExecute = false;
        start.CreateNoWindow = true;
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        using (Process process = Process.Start(start))
        {
            stdout = process.StandardOutput.ReadToEnd();
            stderr = process.StandardError.ReadToEnd();
            process.WaitForExit();
            return process.ExitCode;
        }
    }

    private void PublishToDevice()
    {
        if (dirty && !SaveCurrent()) return;
        string[] ports = SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
        if (ports.Length == 0)
        {
            MessageBox.Show(this, "No serial port was found.", "Publish unavailable",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        string selectedPort = ChooseSerialPort(ports);
        if (String.IsNullOrEmpty(selectedPort)) return;
        DialogResult confirmation = MessageBox.Show(this,
            "Put the device on CONFIG UPLOAD, then continue.\n\n" +
            "The complete approved calibration set will replace the active set on " +
            selectedPort + ". Draft and blocked profiles will not be published.",
            "Publish calibration profiles", MessageBoxButtons.OKCancel,
            MessageBoxIcon.Information);
        if (confirmation != DialogResult.OK) return;

        string projectDir = WorkingDirectory();
        string uploader = Path.Combine(ToolDirectory(),
            "upload_calibration_package.py");
        if (!File.Exists(uploader))
        {
            MessageBox.Show(this, "Calibration uploader was not found:\n" + uploader,
                "Publish unavailable", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        string inputFolder = String.IsNullOrEmpty(currentFolder)
            ? DefaultCalibrationFolder()
            : currentFolder;
        string arguments = Quote(uploader) + " " + Quote(selectedPort) +
            " --input " + Quote(inputFolder);
        string stdout;
        string stderr;
        int exitCode;
        UseWaitCursor = true;
        statusLabel.Text = "Building and publishing calibration bundle...";
        try
        {
            exitCode = RunImporter(projectDir, arguments, out stdout, out stderr);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Publish failed",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        finally
        {
            UseWaitCursor = false;
        }

        string detail = String.IsNullOrWhiteSpace(stderr) ? stdout : stderr + "\n" + stdout;
        if (exitCode != 0)
        {
            statusLabel.Text = "Calibration publish failed";
            MessageBox.Show(this, detail.Trim(), "Publish failed",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        statusLabel.Text = "Calibration profiles published; device restarting";
        MessageBox.Show(this, detail.Trim(), "Publish complete",
            MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    private string ChooseSerialPort(string[] ports)
    {
        using (Form dialog = new Form())
        {
            dialog.Text = "Select device port";
            dialog.StartPosition = FormStartPosition.CenterParent;
            dialog.FormBorderStyle = FormBorderStyle.FixedDialog;
            dialog.MinimizeBox = false;
            dialog.MaximizeBox = false;
            dialog.ClientSize = new Size(330, 120);
            Label label = new Label();
            label.Text = "USB CDC port:";
            label.AutoSize = true;
            label.Location = new Point(14, 18);
            ComboBox choices = new ComboBox();
            choices.DropDownStyle = ComboBoxStyle.DropDownList;
            choices.Items.AddRange(ports);
            choices.SelectedIndex = ports.Length - 1;
            choices.Location = new Point(112, 14);
            choices.Width = 190;
            Button ok = new Button();
            ok.Text = "Publish";
            ok.DialogResult = DialogResult.OK;
            ok.Location = new Point(146, 72);
            ok.Width = 75;
            Button cancel = new Button();
            cancel.Text = "Cancel";
            cancel.DialogResult = DialogResult.Cancel;
            cancel.Location = new Point(227, 72);
            cancel.Width = 75;
            dialog.Controls.Add(label);
            dialog.Controls.Add(choices);
            dialog.Controls.Add(ok);
            dialog.Controls.Add(cancel);
            dialog.AcceptButton = ok;
            dialog.CancelButton = cancel;
            return dialog.ShowDialog(this) == DialogResult.OK
                ? Convert.ToString(choices.SelectedItem)
                : null;
        }
    }

    private static string ToolDirectory()
    {
        return Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory);
    }

    private static string WorkingDirectory()
    {
        string toolDirectory = ToolDirectory();
        string projectDirectory = Path.GetFullPath(Path.Combine(toolDirectory, ".."));
        if (Directory.Exists(Path.Combine(projectDirectory, "config")) &&
            Directory.Exists(Path.Combine(projectDirectory, "tools")))
            return projectDirectory;
        return toolDirectory;
    }

    private static string DefaultCalibrationFolder()
    {
        string portable = Path.Combine(ToolDirectory(), "calibration_profiles");
        if (Directory.Exists(portable)) return portable;
        return Path.Combine(WorkingDirectory(), "config", "calibration_profiles");
    }

    private static string ResolveDataFile(string name)
    {
        string portable = Path.Combine(ToolDirectory(), name);
        if (File.Exists(portable)) return portable;
        return Path.Combine(WorkingDirectory(), "config", name);
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }

    private static string SafeProfileId(string value, string fallback)
    {
        StringBuilder result = new StringBuilder();
        foreach (char character in value.ToLowerInvariant())
        {
            if ((character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') ||
                character == '.' || character == '_' || character == '-')
                result.Append(character);
            else result.Append('_');
        }
        string normalized = result.ToString().Trim('.', '_', '-');
        return normalized.Length == 0 ? fallback : normalized;
    }

    private void ReloadFolder()
    {
        if (String.IsNullOrEmpty(currentFolder)) return;
        if (!ConfirmDiscardOrSave()) return;
        string selected = current == null ? null : current.Path;
        LoadFolder(currentFolder, selected);
    }

    private void LoadFolder(string folder, string selectedPath)
    {
        if (!Directory.Exists(folder))
        {
            MessageBox.Show(this, "Folder not found:\n" + folder, Text,
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        loading = true;
        profiles.Clear();
        profileList.Items.Clear();
        current = null;
        dirty = false;
        currentFolder = Path.GetFullPath(folder);
        folderLabel.Text = currentFolder;
        List<string> errors = new List<string>();
        foreach (string path in Directory.GetFiles(currentFolder, "*.json"))
        {
            try
            {
                string text = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(path));
                IDictionary<string, object> root = MakeMutable(
                    serializer.DeserializeObject(text)) as IDictionary<string, object>;
                if (root == null || !root.ContainsKey("profile_id") || !root.ContainsKey("procedures"))
                    continue;
                CalibrationProfileEntry entry = new CalibrationProfileEntry();
                entry.Path = Path.GetFullPath(path);
                entry.Root = root;
                profiles.Add(entry);
            }
            catch (Exception ex)
            {
                errors.Add(Path.GetFileName(path) + ": " + ex.Message);
            }
        }
        profiles.Sort(delegate(CalibrationProfileEntry left, CalibrationProfileEntry right)
        {
            return String.Compare(left.Path, right.Path, StringComparison.OrdinalIgnoreCase);
        });
        foreach (CalibrationProfileEntry entry in profiles)
            profileList.Items.Add(entry);
        loading = false;

        int selectedIndex = 0;
        if (!String.IsNullOrEmpty(selectedPath))
        {
            selectedIndex = profiles.FindIndex(delegate(CalibrationProfileEntry entry)
            {
                return String.Equals(entry.Path, selectedPath, StringComparison.OrdinalIgnoreCase);
            });
            if (selectedIndex < 0) selectedIndex = 0;
        }
        if (profileList.Items.Count > 0)
        {
            profileList.SelectedIndex = selectedIndex;
            // ListBox keeps its previous scroll position after Items.Clear().
            // Keep an explicitly opened/imported profile visible, especially when
            // it sorts before the previously visible first row (for example PN 10009).
            profileList.TopIndex = selectedIndex;
        }
        else ClearEditor();
        statusLabel.Text = String.Format("Loaded {0} profile(s)", profiles.Count);
        if (errors.Count > 0)
            MessageBox.Show(this, String.Join("\n", errors.ToArray()), "Load warnings",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }

    private void ProfileSelectionChanged(object sender, EventArgs e)
    {
        if (loading) return;
        CalibrationProfileEntry selected = profileList.SelectedItem as CalibrationProfileEntry;
        if (selected == current) return;
        if (!ConfirmDiscardOrSave())
        {
            loading = true;
            profileList.SelectedItem = current;
            loading = false;
            return;
        }
        ShowProfile(selected);
    }

    private void ShowProfile(CalibrationProfileEntry entry)
    {
        loading = true;
        current = entry;
        dirty = false;
        procedureTree.Nodes.Clear();
        resultGrid.Rows.Clear();
        operationList.Items.Clear();
        operationSummary.Clear();
        ClearReviewUi();
        if (entry == null)
        {
            ClearEditor();
            loading = false;
            return;
        }

        object profileId;
        object pn;
        object model;
        object status;
        entry.Root.TryGetValue("profile_id", out profileId);
        entry.Root.TryGetValue("product_num", out pn);
        entry.Root.TryGetValue("model_name", out model);
        entry.Root.TryGetValue("release_status", out status);
        profileLabel.Text = String.Format("{0}  PN {1}  {2}", profileId, pn, model);
        releaseStatus.SelectedItem = Convert.ToString(status);
        if (releaseStatus.SelectedIndex < 0) releaseStatus.SelectedItem = "blocked";

        IList procedures = AsList(Get(entry.Root, "procedures"));
        if (procedures != null)
        {
            int procedureNumber = 0;
            foreach (object procedureObject in procedures)
            {
                IDictionary<string, object> procedure = AsDict(procedureObject);
                if (procedure == null) continue;
                procedureNumber++;
                string procedureName = Convert.ToString(Get(procedure, "name"));
                string readingName = Convert.ToString(Get(procedure, "reading_name"));
                string category = Convert.ToString(Get(procedure, "category"));
                TreeNode procedureNode = new TreeNode(String.Format("[{0}] {1} / {2}",
                    category == "output_span" ? "OUTPUT" : "SENSOR",
                    readingName, procedureName));
                procedureNode.Tag = new JsonNodeBinding
                {
                    Kind = "operation", Value = procedure, Parent = procedures,
                    Index = procedureNumber - 1, Procedure = procedure
                };
                procedureNode.Nodes.Add("ID: " + Get(procedure, "id"));
                procedureNode.Nodes.Add("Message: " + Get(procedure, "message"));
                procedureNode.Nodes.Add("Unit: " + Get(procedure, "unit"));
                procedureNode.Nodes.Add("When: " + FormatCondition(Get(procedure, "when")));
                IList inputs = AsList(Get(procedure, "inputs"));
                TreeNode inputsNode = new TreeNode("Inputs (" + (inputs == null ? 0 : inputs.Count) + ")");
                if (inputs != null)
                {
                    foreach (object inputObject in inputs)
                    {
                        IDictionary<string, object> input = AsDict(inputObject);
                        if (input == null) continue;
                        string left = IsTrue(Get(input, "minimum_inclusive")) ? "[" : "(";
                        string rightBound = IsTrue(Get(input, "maximum_inclusive")) ? "]" : ")";
                        IDictionary<string, object> scale = AsDict(Get(input, "write_scale"));
                        string scaleText = scale == null ? "?" :
                            Convert.ToString(Get(scale, "numerator")) + "/" +
                            Convert.ToString(Get(scale, "denominator"));
                        TreeNode inputNode = new TreeNode(String.Format(
                            "{0}: {1} default={2} range={3}{4}, {5}{6} {7} decimals={8} scale={9}",
                            Get(input, "id"), Get(input, "type"), Get(input, "default"),
                            left, Get(input, "minimum"), Get(input, "maximum"), rightBound,
                            Get(input, "unit"), Get(input, "decimals"), scaleText));
                        inputNode.Tag = new JsonNodeBinding
                        {
                            Kind = "input", Value = input, Parent = inputs,
                            Index = inputsNode.Nodes.Count, Procedure = procedure
                        };
                        inputsNode.Nodes.Add(inputNode);
                    }
                }
                procedureNode.Nodes.Add(inputsNode);

                IList steps = AsList(Get(procedure, "steps"));
                int stepNumber = 0;
                if (steps != null)
                {
                    foreach (object stepObject in steps)
                    {
                        IDictionary<string, object> step = AsDict(stepObject);
                        if (step == null) continue;
                        stepNumber++;
                        TreeNode stepNode = new TreeNode("Step " + stepNumber + "  " + Get(step, "id"));
                        stepNode.Tag = new JsonNodeBinding
                        {
                            Kind = "step", Value = step, Parent = steps,
                            Index = stepNumber - 1, Procedure = procedure
                        };
                        stepNode.Nodes.Add("When: " + FormatCondition(Get(step, "when")));
                        IList alternatives = AsList(Get(step, "alternatives"));
                        if (alternatives != null)
                        {
                            int alternativeNumber = 0;
                            foreach (object alternativeObject in alternatives)
                            {
                                IDictionary<string, object> alternative = AsDict(alternativeObject);
                                if (alternative == null) continue;
                                alternativeNumber++;
                                IDictionary<string, object> write = AsDict(Get(alternative, "write"));
                                string address = write == null ? "?" : Convert.ToString(Get(write, "address"));
                                string type = write == null ? "?" : Convert.ToString(Get(write, "type"));
                                IDictionary<string, object> value = write == null
                                    ? null : AsDict(Get(write, "value"));
                                string valueText = FormatWriteValue(value);
                                TreeNode alternativeNode = new TreeNode(String.Format(
                                    "Alternative {0}: {1} @ {2} <= {3}",
                                    alternativeNumber, type, address, valueText));
                                alternativeNode.Tag = new JsonNodeBinding
                                {
                                    Kind = "alternative", Value = alternative,
                                    Parent = alternatives, Index = alternativeNumber - 1,
                                    Procedure = procedure
                                };
                                alternativeNode.Nodes.Add("When: " +
                                    FormatCondition(Get(alternative, "when")));
                                alternativeNode.Nodes.Add("Wait after: " +
                                    Get(alternative, "wait_after_ms") + " ms");
                                IDictionary<string, object> result =
                                    AsDict(Get(alternative, "result"));
                                if (result == null)
                                {
                                    alternativeNode.Nodes.Add("Result: command/echo completion only");
                                }
                                else
                                {
                                    alternativeNode.Nodes.Add(String.Format(
                                        "Result: {0} @ {1}, poll {2} ms, timeout {3} ms",
                                        Get(result, "type"), Get(result, "address"),
                                        Get(result, "poll_interval_ms"), Get(result, "timeout_ms")));
                                }
                                stepNode.Nodes.Add(alternativeNode);
                                AddResultRows(procedureName, stepNumber, alternative);
                            }
                        }
                        procedureNode.Nodes.Add(stepNode);
                    }
                }
                procedureTree.Nodes.Add(procedureNode);
                operationList.Items.Add(new ReviewOperationItem
                {
                    Procedure = procedure,
                    Number = procedureNumber,
                    Confirmed = IsOperationConfirmed(Convert.ToString(Get(procedure, "id")))
                });
            }
        }
        procedureTree.ExpandAll();
        if (operationList.Items.Count > 0) operationList.SelectedIndex = 0;
        RefreshReviewProgress();
        LoadReviewState();
        RefreshApprovalBlockers();
        UpdateDirtyUi();
        loading = false;
    }

    private void AddResultRows(string procedureName, int stepNumber,
                               IDictionary<string, object> alternative)
    {
        IDictionary<string, object> result = AsDict(Get(alternative, "result"));
        if (result == null) return;
        object address = Get(result, "address");
        IList mappings = AsList(Get(result, "mappings"));
        if (mappings == null) return;
        foreach (object mappingObject in mappings)
        {
            IDictionary<string, object> mapping = AsDict(mappingObject);
            if (mapping == null) continue;
            int rowIndex = resultGrid.Rows.Add(procedureName, stepNumber, address,
                Get(mapping, "value"), Get(mapping, "outcome"), Get(mapping, "message"));
            resultGrid.Rows[rowIndex].Tag = new ResultRowBinding
            {
                Mapping = mapping,
                Parent = mappings
            };
        }
    }

    private void ShowSelectedOperationSummary()
    {
        ReviewOperationItem item = operationList.SelectedItem as ReviewOperationItem;
        if (item == null || item.Procedure == null)
        {
            operationSummary.Text = "Select an operation.";
            confirmOperationButton.Enabled = false;
            return;
        }
        confirmOperationButton.Enabled = true;
        IDictionary<string, object> procedure = item.Procedure;
        StringBuilder text = new StringBuilder();
        string category = Convert.ToString(Get(procedure, "category"));
        text.AppendLine(category == "output_span"
            ? "TYPE: 4-20mA OUTPUT SETTING" : "TYPE: SENSOR CALIBRATION");
        text.AppendLine("NAME: " + Get(procedure, "name"));
        text.AppendLine("READING: " + Get(procedure, "reading_name"));
        text.AppendLine("MESSAGE: " + Get(procedure, "message"));
        text.AppendLine("UNIT: " + Get(procedure, "unit"));
        text.AppendLine("APPLIES: " + FormatCondition(Get(procedure, "when")));
        text.AppendLine();

        IList inputs = AsList(Get(procedure, "inputs"));
        text.AppendLine("INPUTS: " + (inputs == null ? 0 : inputs.Count));
        if (inputs != null)
        {
            foreach (object inputObject in inputs)
            {
                IDictionary<string, object> input = AsDict(inputObject);
                if (input == null) continue;
                text.AppendLine(String.Format(
                    "  {0}: {1}, default {2}, range {3} to {4} {5}",
                    Get(input, "label"), Get(input, "type"), Get(input, "default"),
                    Get(input, "minimum"), Get(input, "maximum"), Get(input, "unit")));
            }
        }
        text.AppendLine();
        text.AppendLine("COMMANDS:");
        IList steps = AsList(Get(procedure, "steps"));
        int number = 0;
        if (steps != null)
        {
            foreach (object stepObject in steps)
            {
                number++;
                IDictionary<string, object> step = AsDict(stepObject);
                IList alternatives = step == null ? null : AsList(Get(step, "alternatives"));
                if (alternatives == null) continue;
                foreach (object alternativeObject in alternatives)
                {
                    IDictionary<string, object> alternative = AsDict(alternativeObject);
                    IDictionary<string, object> write = alternative == null ? null :
                        AsDict(Get(alternative, "write"));
                    IDictionary<string, object> value = write == null ? null :
                        AsDict(Get(write, "value"));
                    text.AppendLine(String.Format("  {0}. Write {1} = {2} ({3})",
                        number, Get(write, "address"), FormatWriteValue(value),
                        Get(write, "type")));
                    text.AppendLine("     Wait " + Get(alternative, "wait_after_ms") + " ms");
                    IDictionary<string, object> result = AsDict(Get(alternative, "result"));
                    if (result != null)
                    {
                        text.AppendLine(String.Format(
                            "     Read result {0}, every {1} ms, timeout {2} ms",
                            Get(result, "address"), Get(result, "poll_interval_ms"),
                            Get(result, "timeout_ms")));
                        IList mappings = AsList(Get(result, "mappings"));
                        if (mappings != null)
                        {
                            foreach (object mappingObject in mappings)
                            {
                                IDictionary<string, object> mapping = AsDict(mappingObject);
                                text.AppendLine(String.Format("       {0} => {1}: {2}",
                                    Get(mapping, "value"), Get(mapping, "outcome"),
                                    Get(mapping, "message")));
                            }
                        }
                    }
                }
            }
        }
        text.AppendLine();
        text.AppendLine(item.Confirmed ? "STATUS: CONFIRMED" : "STATUS: NEEDS REVIEW");
        operationSummary.Text = text.ToString();
        confirmOperationButton.Text = item.Confirmed ? "Confirmed" : "Confirm operation";
    }

    private void ConfirmSelectedOperation()
    {
        ReviewOperationItem item = operationList.SelectedItem as ReviewOperationItem;
        if (item == null) return;
        List<string> errors = ValidateCurrent();
        if (errors.Count > 0)
        {
            ShowValidation(errors);
            return;
        }
        IList confirmed = ConfirmedOperationIds(true);
        string id = Convert.ToString(Get(item.Procedure, "id"));
        if (!ContainsText(confirmed, id)) confirmed.Add(id);
        item.Confirmed = true;
        ForceDraft();
        UpdateAutomaticReviewFlags();
        MarkDirty("Operation confirmed; approve after all operations are confirmed");
        operationList.Refresh();
        ShowSelectedOperationSummary();
        RefreshReviewProgress();
        RefreshApprovalBlockers();
        SaveDraftCheckpoint("Operation confirmation saved");
    }

    private void EditSelectedReviewOperation()
    {
        ReviewOperationItem item = operationList.SelectedItem as ReviewOperationItem;
        if (item == null) return;
        string edited;
        if (!ShowJsonEditor("Edit operation",
            PrettyJson(serializer.Serialize(item.Procedure)), out edited)) return;
        try
        {
            IDictionary<string, object> replacement = MakeMutable(
                serializer.DeserializeObject(edited)) as IDictionary<string, object>;
            if (replacement == null) throw new InvalidDataException("JSON must be an object");
            IList procedures = AsList(Get(current.Root, "procedures"));
            int index = operationList.SelectedIndex;
            procedures[index] = replacement;
            AfterStructuralChange("Operation JSON edited", "all");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Invalid JSON",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void DeleteSelectedReviewOperation()
    {
        ReviewOperationItem item = operationList.SelectedItem as ReviewOperationItem;
        if (item == null || current == null) return;
        string label = Convert.ToString(Get(item.Procedure, "name")) + " (" +
            Convert.ToString(Get(item.Procedure, "reading_name")) + ")";
        if (MessageBox.Show(this,
            "Delete this operation?\n\n" + label +
            "\n\nAll remaining operations must be reviewed again.",
            "Confirm delete", MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning) != DialogResult.Yes) return;
        IList procedures = AsList(Get(current.Root, "procedures"));
        int index = operationList.SelectedIndex;
        if (procedures == null || index < 0 || index >= procedures.Count) return;
        procedures.RemoveAt(index);
        AfterStructuralChange("Operation deleted", "all");
    }

    private void ApproveAndSave()
    {
        if (current == null) return;
        if (!AllOperationsConfirmed())
        {
            MessageBox.Show(this, "Confirm every operation before approval.",
                "Review incomplete", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        UpdateAutomaticReviewFlags();
        List<string> blockers = GetApprovalBlockers();
        if (blockers.Count > 0) { ShowApprovalBlockers(blockers); return; }
        loading = true;
        releaseStatus.SelectedItem = "approved";
        current.Root["release_status"] = "approved";
        loading = false;
        dirty = true;
        SaveCurrent();
    }

    private void AddOperation()
    {
        if (current == null) return;
        IList procedures = AsList(Get(current.Root, "procedures"));
        if (procedures == null || procedures.Count >= 16)
        {
            MessageBox.Show(this, "Schema supports at most 16 operations.", Text);
            return;
        }
        bool output = MessageBox.Show(this,
            "Create a 4-20mA output operation?\nChoose No for sensor calibration.",
            "New operation", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
        int sequence = procedures.Count + 1;
        IDictionary<string, object> operation = new Dictionary<string, object>();
        operation["id"] = output ? "output_span_new_" + sequence : "calibration_new_" + sequence;
        operation["category"] = output ? "output_span" : "sensor_calibration";
        operation["reading_name"] = output ? "4-20mA Output" : "New reading";
        operation["name"] = output ? "20mA SET" : "New calibration";
        operation["message"] = "Review instructions before use";
        operation["unit"] = "";
        operation["when"] = null;
        operation["inputs"] = new ArrayList();
        operation["steps"] = new ArrayList();
        procedures.Add(operation);
        AfterStructuralChange("Operation added", "all");
    }

    private IDictionary<string, object> SelectedProcedure()
    {
        TreeNode node = procedureTree.SelectedNode;
        while (node != null)
        {
            JsonNodeBinding binding = node.Tag as JsonNodeBinding;
            if (binding != null && binding.Procedure != null) return binding.Procedure;
            node = node.Parent;
        }
        return null;
    }

    private void AddInput()
    {
        IDictionary<string, object> procedure = SelectedProcedure();
        if (procedure == null)
        {
            MessageBox.Show(this, "Select an operation first.", Text);
            return;
        }
        IList inputs = AsList(Get(procedure, "inputs"));
        if (inputs == null || inputs.Count >= 3)
        {
            MessageBox.Show(this, "An operation supports at most three inputs.", Text);
            return;
        }
        int sequence = inputs.Count + 1;
        IDictionary<string, object> input = new Dictionary<string, object>();
        input["id"] = "input_" + sequence;
        input["label"] = "New value";
        input["type"] = "uint16";
        input["default"] = 1;
        input["minimum"] = 0;
        input["maximum"] = 65535;
        input["minimum_inclusive"] = true;
        input["maximum_inclusive"] = true;
        input["decimals"] = 0;
        input["unit"] = Convert.ToString(Get(procedure, "unit"));
        input["write_scale"] = new Dictionary<string, object>
        {
            { "numerator", 1 }, { "denominator", 1 }
        };
        inputs.Add(input);
        AfterStructuralChange("Input added", "input");
    }

    private void AddStep()
    {
        IDictionary<string, object> procedure = SelectedProcedure();
        if (procedure == null)
        {
            MessageBox.Show(this, "Select an operation first.", Text);
            return;
        }
        IList steps = AsList(Get(procedure, "steps"));
        if (steps == null || steps.Count >= 16)
        {
            MessageBox.Show(this, "An operation supports at most 16 steps.", Text);
            return;
        }
        IDictionary<string, object> value = new Dictionary<string, object>
        {
            { "kind", "fixed" }, { "number", 0 }
        };
        IDictionary<string, object> write = new Dictionary<string, object>
        {
            { "address", 1 }, { "type", "uint16" }, { "value", value }
        };
        IDictionary<string, object> alternative = new Dictionary<string, object>
        {
            { "when", null }, { "write", write },
            { "wait_after_ms", 0 }, { "result", null }
        };
        IDictionary<string, object> step = new Dictionary<string, object>
        {
            { "id", "step_" + steps.Count }, { "when", null },
            { "alternatives", new ArrayList { alternative } }
        };
        steps.Add(step);
        AfterStructuralChange("Step added; edit address and value before approval", "command");
    }

    private void EditSelectedJson()
    {
        JsonNodeBinding binding = procedureTree.SelectedNode == null ? null :
            procedureTree.SelectedNode.Tag as JsonNodeBinding;
        if (binding == null || binding.Value == null)
        {
            MessageBox.Show(this, "Select an operation, input, step, or alternative.", Text);
            return;
        }
        string edited;
        string original = PrettyJson(serializer.Serialize(binding.Value));
        if (!ShowJsonEditor("Edit " + binding.Kind, original, out edited)) return;
        try
        {
            IDictionary<string, object> replacement = MakeMutable(
                serializer.DeserializeObject(edited)) as IDictionary<string, object>;
            if (replacement == null) throw new InvalidDataException("JSON must be an object");
            if (binding.Parent == null) throw new InvalidOperationException("Node cannot be replaced");
            binding.Parent[binding.Index] = replacement;
            AfterStructuralChange(binding.Kind + " edited", "all");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Invalid JSON",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void DeleteSelectedNode()
    {
        JsonNodeBinding binding = procedureTree.SelectedNode == null ? null :
            procedureTree.SelectedNode.Tag as JsonNodeBinding;
        if (binding == null || binding.Parent == null)
        {
            MessageBox.Show(this, "Select an operation, input, step, or alternative.", Text);
            return;
        }
        if (MessageBox.Show(this, "Delete selected " + binding.Kind + "?",
            "Confirm delete", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            return;
        binding.Parent.RemoveAt(binding.Index);
        AfterStructuralChange(binding.Kind + " deleted", "all");
    }

    private void MoveSelectedNode(int direction)
    {
        JsonNodeBinding binding = procedureTree.SelectedNode == null ? null :
            procedureTree.SelectedNode.Tag as JsonNodeBinding;
        if (binding == null || binding.Parent == null) return;
        int target = binding.Index + direction;
        if (target < 0 || target >= binding.Parent.Count) return;
        object temporary = binding.Parent[binding.Index];
        binding.Parent[binding.Index] = binding.Parent[target];
        binding.Parent[target] = temporary;
        AfterStructuralChange(binding.Kind + " reordered", "all");
    }

    private void AddResultMapping()
    {
        JsonNodeBinding nodeBinding = procedureTree.SelectedNode == null ? null :
            procedureTree.SelectedNode.Tag as JsonNodeBinding;
        IDictionary<string, object> alternative = null;
        if (nodeBinding != null && nodeBinding.Kind == "alternative")
            alternative = nodeBinding.Value;
        if (alternative == null)
        {
            MessageBox.Show(this, "Select an Alternative node first.", Text);
            return;
        }
        IDictionary<string, object> result = AsDict(Get(alternative, "result"));
        if (result == null)
        {
            result = new Dictionary<string, object>();
            result["address"] = 1;
            result["type"] = "uint16";
            result["register_count"] = 1;
            result["poll_interval_ms"] = 500;
            result["timeout_ms"] = 120000;
            result["mappings"] = new ArrayList();
            alternative["result"] = result;
        }
        IList mappings = AsList(Get(result, "mappings"));
        if (mappings == null || mappings.Count >= 8)
        {
            MessageBox.Show(this, "A result supports at most eight mappings.", Text);
            return;
        }
        mappings.Add(new Dictionary<string, object>
        {
            { "value", 0 }, { "outcome", "failure" },
            { "message", "Review result message" }
        });
        AfterStructuralChange("Result mapping added", "result");
    }

    private void DeleteResultMapping()
    {
        if (resultGrid.CurrentRow == null) return;
        ResultRowBinding binding = resultGrid.CurrentRow.Tag as ResultRowBinding;
        if (binding == null || binding.Parent == null) return;
        if (MessageBox.Show(this, "Delete selected result mapping?", "Confirm delete",
            MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
        binding.Parent.Remove(binding.Mapping);
        AfterStructuralChange("Result mapping deleted", "result");
    }

    private void AfterStructuralChange(string status, string reviewArea)
    {
        ForceDraft();
        IDictionary<string, object> review = current == null ? null :
            AsDict(Get(current.Root, "review"));
        if (review != null)
        {
            if (reviewArea == "all" || reviewArea == "input") review["input_ranges"] = false;
            if (reviewArea == "all" || reviewArea == "result") review["result_mappings"] = false;
            if (reviewArea == "all") review["version_conditions"] = false;
            if (reviewArea == "all" || reviewArea == "command") review["command_sequences"] = false;
            review["confirmed_operations"] = new ArrayList();
        }
        MarkDirty(status + "; profile changed to draft");
        ShowProfilePreservingDirty();
        SaveDraftCheckpoint(status + " and draft saved");
    }

    private bool SaveDraftCheckpoint(string successStatus)
    {
        if (current == null) return false;
        if (Convert.ToString(Get(current.Root, "release_status")) != "draft")
            ForceDraft();
        if (!SaveCurrent()) return false;
        statusLabel.Text = successStatus;
        return true;
    }

    private void ForceDraft()
    {
        if (current == null) return;
        current.Root["release_status"] = "draft";
        loading = true;
        releaseStatus.SelectedItem = "draft";
        loading = false;
    }

    private void ShowProfilePreservingDirty()
    {
        CalibrationProfileEntry entry = current;
        ShowProfile(entry);
        dirty = true;
        UpdateDirtyUi();
    }

    private bool ShowJsonEditor(string title, string initial, out string result)
    {
        result = null;
        using (Form dialog = new Form())
        {
            dialog.Text = title;
            dialog.StartPosition = FormStartPosition.CenterParent;
            dialog.Size = new Size(760, 620);
            dialog.MinimizeBox = false;
            dialog.MaximizeBox = true;
            TextBox editor = new TextBox();
            editor.Multiline = true;
            editor.AcceptsReturn = true;
            editor.AcceptsTab = true;
            editor.ScrollBars = ScrollBars.Both;
            editor.WordWrap = false;
            editor.Font = new Font("Consolas", 10F);
            editor.Dock = DockStyle.Fill;
            editor.Text = initial;
            FlowLayoutPanel buttons = new FlowLayoutPanel();
            buttons.Dock = DockStyle.Bottom;
            buttons.Height = 42;
            buttons.FlowDirection = FlowDirection.RightToLeft;
            Button ok = new Button();
            ok.Text = "Apply";
            ok.DialogResult = DialogResult.OK;
            Button cancel = new Button();
            cancel.Text = "Cancel";
            cancel.DialogResult = DialogResult.Cancel;
            buttons.Controls.Add(ok);
            buttons.Controls.Add(cancel);
            dialog.Controls.Add(editor);
            dialog.Controls.Add(buttons);
            dialog.AcceptButton = ok;
            dialog.CancelButton = cancel;
            if (dialog.ShowDialog(this) != DialogResult.OK) return false;
            result = editor.Text;
            return true;
        }
    }

    private void ResultCellValueChanged(object sender, DataGridViewCellEventArgs e)
    {
        if (loading || e.RowIndex < 0 ||
            (e.ColumnIndex != 3 && e.ColumnIndex != 4 && e.ColumnIndex != 5)) return;
        ResultRowBinding binding = resultGrid.Rows[e.RowIndex].Tag as ResultRowBinding;
        if (binding == null || binding.Mapping == null) return;
        if (e.ColumnIndex == 3)
        {
            int value;
            string text = Convert.ToString(resultGrid.Rows[e.RowIndex].Cells[3].Value);
            binding.Mapping["value"] = Int32.TryParse(text, out value)
                ? (object)value : text;
        }
        else if (e.ColumnIndex == 4)
            binding.Mapping["outcome"] = Convert.ToString(resultGrid.Rows[e.RowIndex].Cells[4].Value);
        else
            binding.Mapping["message"] = Convert.ToString(resultGrid.Rows[e.RowIndex].Cells[5].Value);
        SetReviewFlag("result_mappings", false);
        ClearConfirmedOperations();
        loading = true;
        reviewChecks.SetItemChecked(ReviewResultMappings, false);
        loading = false;
        MarkDirty("Outcome changed; validate and approve before release");
        RefreshApprovalBlockers();
    }

    private void ReleaseStatusChanged(object sender, EventArgs e)
    {
        if (loading || current == null) return;
        string selected = Convert.ToString(releaseStatus.SelectedItem);
        if (selected == "approved")
        {
            List<string> blockers = GetApprovalBlockers();
            if (blockers.Count > 0)
            {
                loading = true;
                releaseStatus.SelectedItem = Convert.ToString(Get(current.Root, "release_status"));
                loading = false;
                ShowApprovalBlockers(blockers);
                return;
            }
        }
        current.Root["release_status"] = selected;
        MarkDirty("Release status changed");
        RefreshApprovalBlockers();
    }

    private void ApproveCurrent()
    {
        if (current == null) return;
        List<string> errors = ValidateCurrent();
        if (errors.Count > 0)
        {
            ShowValidation(errors);
            return;
        }
        List<string> blockers = GetApprovalBlockers();
        if (blockers.Count > 0)
        {
            ShowApprovalBlockers(blockers);
            return;
        }
        loading = true;
        releaseStatus.SelectedItem = "approved";
        current.Root["release_status"] = "approved";
        loading = false;
        MarkDirty("Profile approved locally; save to persist the review");
    }

    private void ValidateAndReport()
    {
        if (current == null) return;
        List<string> errors = ValidateCurrent();
        if (errors.Count == 0)
        {
            statusLabel.Text = "Validation passed";
            MessageBox.Show(this, "The current normalized profile is valid.",
                "Validation", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        else ShowValidation(errors);
    }

    private List<string> ValidateCurrent()
    {
        List<string> errors = new List<string>();
        if (current == null) return errors;
        IList procedures = AsList(Get(current.Root, "procedures"));
        if (procedures == null || procedures.Count == 0)
            errors.Add("procedures must not be empty");
        if (procedures != null && procedures.Count > 16)
            errors.Add("procedures exceeds Schema maximum 16");
        HashSet<string> ids = new HashSet<string>();
        if (procedures != null)
        {
            int procedureIndex = 0;
            foreach (object procedureObject in procedures)
            {
                IDictionary<string, object> procedure = AsDict(procedureObject);
                if (procedure == null) { errors.Add("Invalid procedure object"); continue; }
                string id = Convert.ToString(Get(procedure, "id"));
                if (String.IsNullOrWhiteSpace(id) || !ids.Add(id))
                    errors.Add("Procedure id is empty or duplicated: " + id);
                string category = Convert.ToString(Get(procedure, "category"));
                if (category != "sensor_calibration" && category != "output_span")
                    errors.Add(id + ": invalid category " + category);
                ValidateCondition(id + ".when", Get(procedure, "when"), errors);
                IList inputs = AsList(Get(procedure, "inputs"));
                HashSet<string> inputIds = new HashSet<string>();
                if (inputs != null)
                {
                    if (inputs.Count > 3) errors.Add(id + ": exceeds three inputs");
                    foreach (object inputObject in inputs)
                        ValidateInput(id, AsDict(inputObject), inputIds, errors);
                }
                IList steps = AsList(Get(procedure, "steps"));
                if (steps == null || steps.Count == 0)
                    errors.Add("Procedure " + id + " has no steps");
                if (steps != null)
                {
                    foreach (object stepObject in steps)
                    {
                        IDictionary<string, object> step = AsDict(stepObject);
                        if (step != null)
                            ValidateCondition(id + ".step.when", Get(step, "when"), errors);
                        IList alternatives = step == null ? null : AsList(Get(step, "alternatives"));
                        if (alternatives == null || alternatives.Count == 0)
                        {
                            errors.Add("Procedure " + id + " has a step without alternatives");
                            continue;
                        }
                        foreach (object alternativeObject in alternatives)
                        {
                            IDictionary<string, object> alternative = AsDict(alternativeObject);
                            if (alternative == null)
                            {
                                errors.Add(id + ": invalid alternative");
                                continue;
                            }
                            ValidateCondition(id + ".alternative.when",
                                Get(alternative, "when"), errors);
                            ValidateWrite(id, alternative, inputIds, errors);
                            IDictionary<string, object> result = alternative == null
                                ? null : AsDict(Get(alternative, "result"));
                            ValidateResult(id, result, errors);
                        }
                    }
                }
                procedureIndex++;
            }
        }
        return errors;
    }

    private static void ValidateInput(string procedureId,
                                      IDictionary<string, object> input,
                                      HashSet<string> ids,
                                      List<string> errors)
    {
        if (input == null) { errors.Add(procedureId + ": invalid input"); return; }
        string id = Convert.ToString(Get(input, "id"));
        if (String.IsNullOrWhiteSpace(id) || !ids.Add(id))
            errors.Add(procedureId + ": empty or duplicate input id " + id);
        string type = Convert.ToString(Get(input, "type"));
        if (type != "uint16" && type != "float32")
            errors.Add(procedureId + ": unsupported input type " + type);
        double minimum = 0;
        double maximum = 0;
        double defaultValue = 0;
        if (!Double.TryParse(Convert.ToString(Get(input, "minimum")), out minimum) ||
            !Double.TryParse(Convert.ToString(Get(input, "maximum")), out maximum) ||
            !Double.TryParse(Convert.ToString(Get(input, "default")), out defaultValue) ||
            minimum > maximum || defaultValue < minimum || defaultValue > maximum)
            errors.Add(procedureId + ": invalid input bounds/default for " + id);
        if (type == "uint16" &&
            (minimum < 0 || maximum > 65535 || minimum != Math.Truncate(minimum) ||
             maximum != Math.Truncate(maximum) || defaultValue != Math.Truncate(defaultValue)))
            errors.Add(procedureId + ": UInt16 range/default is not integral for " + id);
        IDictionary<string, object> scale = AsDict(Get(input, "write_scale"));
        double denominator;
        if (scale == null ||
            !Double.TryParse(Convert.ToString(Get(scale, "denominator")), out denominator) ||
            denominator == 0)
            errors.Add(procedureId + ": invalid write_scale for " + id);
    }

    private static void ValidateWrite(string procedureId,
                                      IDictionary<string, object> alternative,
                                      HashSet<string> inputIds,
                                      List<string> errors)
    {
        IDictionary<string, object> write = AsDict(Get(alternative, "write"));
        if (write == null) { errors.Add(procedureId + ": missing write command"); return; }
        int address;
        string type = Convert.ToString(Get(write, "type"));
        if (!Int32.TryParse(Convert.ToString(Get(write, "address")), out address) ||
            address < 1 || address > 65536 || (type == "float32_cdab" && address == 65536))
            errors.Add(procedureId + ": invalid write address");
        if (type != "uint16" && type != "float32_cdab")
            errors.Add(procedureId + ": unsupported write type " + type);
        IDictionary<string, object> value = AsDict(Get(write, "value"));
        string kind = value == null ? "" : Convert.ToString(Get(value, "kind"));
        if (kind == "input")
        {
            string inputId = Convert.ToString(Get(value, "input_id"));
            if (!inputIds.Contains(inputId))
                errors.Add(procedureId + ": unknown input reference " + inputId);
        }
        else if (kind == "fixed")
        {
            double number;
            if (!Double.TryParse(Convert.ToString(Get(value, "number")), out number))
                errors.Add(procedureId + ": invalid fixed write value");
        }
        else errors.Add(procedureId + ": ambiguous write value source");
        int wait;
        if (!Int32.TryParse(Convert.ToString(Get(alternative, "wait_after_ms")), out wait) ||
            wait < 0 || wait > 300000)
            errors.Add(procedureId + ": invalid wait_after_ms");
    }

    private static void ValidateCondition(string path, object value,
                                          List<string> errors)
    {
        if (value == null) return;
        IDictionary<string, object> condition = AsDict(value);
        if (condition == null)
        {
            errors.Add(path + ": invalid condition");
            return;
        }
        if (condition.ContainsKey("not"))
        {
            ValidateCondition(path + ".not", Get(condition, "not"), errors);
            return;
        }
        IList all = AsList(Get(condition, "all"));
        if (all == null || all.Count == 0)
            errors.Add(path + ": condition must contain predicates");
    }

    private static void ValidateResult(string procedureId,
                                       IDictionary<string, object> result,
                                       List<string> errors)
    {
        if (result == null) return;
        IList mappings = AsList(Get(result, "mappings"));
        if (mappings == null || mappings.Count == 0)
        {
            errors.Add(procedureId + ": result has no mappings");
            return;
        }
        HashSet<int> values = new HashSet<int>();
        int successes = 0;
        foreach (object mappingObject in mappings)
        {
            IDictionary<string, object> mapping = AsDict(mappingObject);
            if (mapping == null) { errors.Add(procedureId + ": invalid mapping"); continue; }
            int value;
            if (!Int32.TryParse(Convert.ToString(Get(mapping, "value")), out value) ||
                value < 0 || value > 65535 || !values.Add(value))
                errors.Add(procedureId + ": invalid or duplicate result value");
            string outcome = Convert.ToString(Get(mapping, "outcome"));
            if (outcome != "success" && outcome != "failure" &&
                outcome != "pending" && outcome != "info")
                errors.Add(procedureId + ": invalid outcome " + outcome);
            if (outcome == "success") successes++;
            if (String.IsNullOrWhiteSpace(Convert.ToString(Get(mapping, "message"))))
                errors.Add(procedureId + ": empty result message");
        }
        if (successes == 0) errors.Add(procedureId + ": result has no success outcome");
    }

    private void ReviewItemCheck(object sender, ItemCheckEventArgs e)
    {
        if (loading || current == null) return;
        string key = e.Index == ReviewInputRanges ? "input_ranges" :
            e.Index == ReviewResultMappings ? "result_mappings" :
            e.Index == ReviewVersionConditions ? "version_conditions" :
            "command_sequences";
        SetReviewFlag(key, e.NewValue == CheckState.Checked);
        MarkDirty("Review checklist changed");
        BeginInvoke(new Action(RefreshApprovalBlockers));
    }

    private void LoadReviewState()
    {
        IDictionary<string, object> review = AsDict(Get(current.Root, "review"));
        reviewChecks.SetItemChecked(ReviewInputRanges,
            IsTrue(Get(review, "input_ranges")));
        reviewChecks.SetItemChecked(ReviewResultMappings,
            IsTrue(Get(review, "result_mappings")));
        reviewChecks.SetItemChecked(ReviewVersionConditions,
            IsTrue(Get(review, "version_conditions")));
        reviewChecks.SetItemChecked(ReviewCommandSequences,
            IsTrue(Get(review, "command_sequences")));
    }

    private void SetReviewFlag(string key, bool value)
    {
        if (current == null) return;
        IDictionary<string, object> review = AsDict(Get(current.Root, "review"));
        if (review == null)
        {
            review = new Dictionary<string, object>();
            current.Root["review"] = review;
        }
        review[key] = value;
    }

    private List<string> GetApprovalBlockers()
    {
        List<string> blockers = ValidateCurrent();
        if (current == null) return blockers;
        if (!AllOperationsConfirmed())
            blockers.Add("Not all operations have been confirmed in Review mode");
        object requiresVersion = Get(current.Root, "requires_version_read");
        if (IsTrue(requiresVersion))
            blockers.Add("Firmware does not yet support version-conditioned profiles");

        IList procedures = AsList(Get(current.Root, "procedures"));
        bool hasInputs = false;
        bool hasResults = false;
        bool hasConditions = IsTrue(requiresVersion);
        if (procedures != null)
        {
            foreach (object procedureObject in procedures)
            {
                IDictionary<string, object> procedure = AsDict(procedureObject);
                if (procedure == null) continue;
                IList inputs = AsList(Get(procedure, "inputs"));
                hasInputs |= inputs != null && inputs.Count > 0;
                hasConditions |= Get(procedure, "when") != null;
                IList steps = AsList(Get(procedure, "steps"));
                if (steps == null) continue;
                foreach (object stepObject in steps)
                {
                    IDictionary<string, object> step = AsDict(stepObject);
                    if (step == null) continue;
                    hasConditions |= Get(step, "when") != null;
                    IList alternatives = AsList(Get(step, "alternatives"));
                    if (alternatives == null) continue;
                    foreach (object alternativeObject in alternatives)
                    {
                        IDictionary<string, object> alternative = AsDict(alternativeObject);
                        if (alternative == null) continue;
                        hasConditions |= Get(alternative, "when") != null;
                        hasResults |= AsDict(Get(alternative, "result")) != null;
                    }
                }
            }
        }
        if (procedures != null && procedures.Count > 10)
            blockers.Add("UI supports at most ten operations on two pages");

        IDictionary<string, object> review = AsDict(Get(current.Root, "review"));
        if (hasInputs && !IsTrue(Get(review, "input_ranges")))
            blockers.Add("Input ranges and value types have not been reviewed");
        if (hasResults && !IsTrue(Get(review, "result_mappings")))
            blockers.Add("Result mappings and outcomes have not been reviewed");
        if (hasConditions && !IsTrue(Get(review, "version_conditions")))
            blockers.Add("Version conditions have not been reviewed");
        if (!IsTrue(Get(review, "command_sequences")))
            blockers.Add("Command addresses, values, and waits have not been reviewed");

        IList auditIssues = AsList(Get(current.Root, "audit_issues"));
        if (auditIssues != null)
        {
            foreach (object issueObject in auditIssues)
            {
                IDictionary<string, object> issue = AsDict(issueObject);
                if (issue == null) continue;
                string severity = Convert.ToString(Get(issue, "severity"));
                if (severity == "error" || severity == "unsupported")
                    blockers.Add("Import " + severity + ": " +
                        Convert.ToString(Get(issue, "message")));
            }
        }

        object pn = Get(current.Root, "product_num");
        int samePn = profiles.FindAll(delegate(CalibrationProfileEntry entry)
        {
            return Convert.ToString(Get(entry.Root, "product_num")) == Convert.ToString(pn);
        }).Count;
        if (samePn > 1) blockers.Add("Duplicate PN exists in the open profile folder");
        return Unique(blockers);
    }

    private void RefreshApprovalBlockers()
    {
        if (current == null) { blockerText.Text = ""; return; }
        List<string> blockers = GetApprovalBlockers();
        blockerText.ForeColor = blockers.Count == 0 ? Color.DarkGreen : Color.DarkRed;
        blockerText.Text = blockers.Count == 0
            ? "READY FOR APPROVAL"
            : "APPROVAL BLOCKERS:" + Environment.NewLine +
              "- " + String.Join(Environment.NewLine + "- ", blockers.ToArray());
    }

    private void ShowApprovalBlockers(List<string> blockers)
    {
        RefreshApprovalBlockers();
        MessageBox.Show(this,
            "Profile cannot be approved:" + Environment.NewLine + "- " +
            String.Join(Environment.NewLine + "- ", blockers.ToArray()),
            "Approval blocked", MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }

    private static List<string> Unique(List<string> values)
    {
        List<string> result = new List<string>();
        HashSet<string> seen = new HashSet<string>();
        foreach (string value in values)
            if (seen.Add(value)) result.Add(value);
        return result;
    }

    private void ClearReviewUi()
    {
        for (int index = 0; index < reviewChecks.Items.Count; index++)
            reviewChecks.SetItemChecked(index, false);
        blockerText.Text = "";
    }

    private IList ConfirmedOperationIds(bool create)
    {
        if (current == null) return null;
        IDictionary<string, object> review = AsDict(Get(current.Root, "review"));
        if (review == null && create)
        {
            review = new Dictionary<string, object>();
            current.Root["review"] = review;
        }
        IList ids = review == null ? null : AsList(Get(review, "confirmed_operations"));
        if (ids == null && create)
        {
            ids = new ArrayList();
            review["confirmed_operations"] = ids;
        }
        return ids;
    }

    private bool IsOperationConfirmed(string id)
    {
        return ContainsText(ConfirmedOperationIds(false), id);
    }

    private static bool ContainsText(IList values, string text)
    {
        if (values == null) return false;
        foreach (object value in values)
            if (String.Equals(Convert.ToString(value), text, StringComparison.Ordinal))
                return true;
        return false;
    }

    private bool AllOperationsConfirmed()
    {
        IList procedures = current == null ? null : AsList(Get(current.Root, "procedures"));
        IList confirmed = ConfirmedOperationIds(false);
        if (procedures == null || procedures.Count == 0) return false;
        foreach (object procedureObject in procedures)
        {
            IDictionary<string, object> procedure = AsDict(procedureObject);
            if (procedure == null ||
                !ContainsText(confirmed, Convert.ToString(Get(procedure, "id")))) return false;
        }
        return true;
    }

    private void UpdateAutomaticReviewFlags()
    {
        if (current == null || !AllOperationsConfirmed()) return;
        SetReviewFlag("input_ranges", true);
        SetReviewFlag("result_mappings", true);
        SetReviewFlag("version_conditions", true);
        SetReviewFlag("command_sequences", true);
        loading = true;
        LoadReviewState();
        loading = false;
    }

    private void RefreshReviewProgress()
    {
        int total = operationList.Items.Count;
        int confirmed = 0;
        foreach (object value in operationList.Items)
        {
            ReviewOperationItem item = value as ReviewOperationItem;
            if (item != null && item.Confirmed) confirmed++;
        }
        reviewProgressLabel.Text = String.Format("Reviewed {0} of {1} operations",
            confirmed, total);
    }

    private void ClearConfirmedOperations()
    {
        IList confirmed = ConfirmedOperationIds(false);
        if (confirmed != null) confirmed.Clear();
        foreach (object value in operationList.Items)
        {
            ReviewOperationItem item = value as ReviewOperationItem;
            if (item != null) item.Confirmed = false;
        }
        operationList.Refresh();
        RefreshReviewProgress();
    }

    private void ShowValidation(List<string> errors)
    {
        statusLabel.Text = "Validation failed: " + errors.Count + " error(s)";
        MessageBox.Show(this, String.Join("\n", errors.ToArray()), "Validation errors",
            MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    private bool SaveCurrent()
    {
        if (current == null) return true;
        List<string> errors = ValidateCurrent();
        if (errors.Count > 0)
        {
            ShowValidation(errors);
            return false;
        }
        if (Convert.ToString(releaseStatus.SelectedItem) == "approved")
        {
            List<string> blockers = GetApprovalBlockers();
            if (blockers.Count > 0)
            {
                ShowApprovalBlockers(blockers);
                return false;
            }
        }
        try
        {
            current.Root["release_status"] = Convert.ToString(releaseStatus.SelectedItem);
            string compact = serializer.Serialize(current.Root);
            string formatted = PrettyJson(compact) + Environment.NewLine;
            File.WriteAllText(current.Path, formatted, new UTF8Encoding(false));
            dirty = false;
            UpdateDirtyUi();
            profileList.Refresh();
            statusLabel.Text = "Saved " + Path.GetFileName(current.Path);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Save failed",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    private bool ConfirmDiscardOrSave()
    {
        if (!dirty) return true;
        DialogResult answer = MessageBox.Show(this,
            "Save changes to the current profile?", Text,
            MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
        if (answer == DialogResult.Cancel) return false;
        if (answer == DialogResult.Yes) return SaveCurrent();
        return true;
    }

    private void OnFormClosing(object sender, FormClosingEventArgs e)
    {
        if (!ConfirmDiscardOrSave()) e.Cancel = true;
    }

    private void MarkDirty(string status)
    {
        dirty = true;
        statusLabel.Text = status;
        UpdateDirtyUi();
    }

    private void UpdateDirtyUi()
    {
        saveButton.Enabled = current != null && dirty;
        Text = "Calibration Profile Manager" + (dirty ? " *" : "");
    }

    private void ClearEditor()
    {
        current = null;
        profileLabel.Text = "No profile selected";
        releaseStatus.SelectedIndex = -1;
        procedureTree.Nodes.Clear();
        resultGrid.Rows.Clear();
        operationList.Items.Clear();
        operationSummary.Clear();
        reviewProgressLabel.Text = "";
        dirty = false;
        UpdateDirtyUi();
    }

    private static object Get(IDictionary<string, object> dictionary, string key)
    {
        if (dictionary == null) return null;
        object value;
        return dictionary.TryGetValue(key, out value) ? value : null;
    }

    internal static object Value(IDictionary<string, object> dictionary, string key)
    {
        return Get(dictionary, key);
    }

    private static IDictionary<string, object> AsDict(object value)
    {
        return value as IDictionary<string, object>;
    }

    private static IList AsList(object value)
    {
        return value as IList;
    }

    private static object MakeMutable(object value)
    {
        IDictionary<string, object> dictionary = value as IDictionary<string, object>;
        if (dictionary != null)
        {
            List<string> keys = new List<string>(dictionary.Keys);
            foreach (string key in keys)
                dictionary[key] = MakeMutable(dictionary[key]);
            return dictionary;
        }
        IList list = value as IList;
        if (list != null)
        {
            ArrayList mutable = new ArrayList();
            foreach (object item in list)
                mutable.Add(MakeMutable(item));
            return mutable;
        }
        return value;
    }

    private static bool IsTrue(object value)
    {
        return value is bool && (bool)value;
    }

    private static string FormatWriteValue(IDictionary<string, object> value)
    {
        if (value == null) return "?";
        string kind = Convert.ToString(Get(value, "kind"));
        if (kind == "input") return "input:" + Get(value, "input_id");
        if (kind == "fixed") return "fixed:" + Get(value, "number");
        return "ambiguous";
    }

    private static string FormatCondition(object value)
    {
        if (value == null) return "always";
        IDictionary<string, object> condition = AsDict(value);
        if (condition == null) return "invalid";
        if (condition.ContainsKey("not"))
            return "NOT (" + FormatCondition(Get(condition, "not")) + ")";
        IList predicates = AsList(Get(condition, "all"));
        if (predicates == null || predicates.Count == 0) return "invalid";
        List<string> parts = new List<string>();
        foreach (object predicateObject in predicates)
        {
            IDictionary<string, object> predicate = AsDict(predicateObject);
            if (predicate == null) { parts.Add("invalid"); continue; }
            parts.Add(Convert.ToString(Get(predicate, "field")) + " " +
                Convert.ToString(Get(predicate, "operator")) + " " +
                Convert.ToString(Get(predicate, "value")));
        }
        return String.Join(" AND ", parts.ToArray());
    }

    private static string PrettyJson(string compact)
    {
        StringBuilder output = new StringBuilder(compact.Length * 2);
        int indent = 0;
        bool inString = false;
        bool escaped = false;
        for (int index = 0; index < compact.Length; index++)
        {
            char character = compact[index];
            if (inString)
            {
                output.Append(character);
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') inString = false;
                continue;
            }
            if (character == '"')
            {
                inString = true;
                output.Append(character);
            }
            else if (character == '{' || character == '[')
            {
                output.Append(character).AppendLine();
                indent++;
                AppendIndent(output, indent);
            }
            else if (character == '}' || character == ']')
            {
                output.AppendLine();
                indent--;
                AppendIndent(output, indent);
                output.Append(character);
            }
            else if (character == ',')
            {
                output.Append(character).AppendLine();
                AppendIndent(output, indent);
            }
            else if (character == ':')
            {
                output.Append(": ");
            }
            else if (!Char.IsWhiteSpace(character))
            {
                output.Append(character);
            }
        }
        return output.ToString();
    }

    private static void AppendIndent(StringBuilder output, int indent)
    {
        output.Append(' ', indent * 2);
    }
}
