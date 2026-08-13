using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
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

internal sealed class CalibrationProfileManagerForm : Form
{
    private readonly JavaScriptSerializer serializer = new JavaScriptSerializer();
    private readonly ListBox profileList = new ListBox();
    private readonly TreeView procedureTree = new TreeView();
    private readonly DataGridView resultGrid = new DataGridView();
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
            path = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                "..", "config", "calibration_profiles"));
        BeginInvoke(new Action(delegate { OpenInitialPath(path); }));
    }

    private void BuildUi()
    {
        ToolStrip toolbar = new ToolStrip();
        ToolStripButton open = new ToolStripButton("Open Folder");
        ToolStripButton reload = new ToolStripButton("Reload");
        ToolStripButton validate = new ToolStripButton("Validate");
        ToolStripButton approve = new ToolStripButton("Approve Profile");
        ToolStripButton save = new ToolStripButton("Save");
        open.Click += delegate { ChooseFolder(); };
        reload.Click += delegate { ReloadFolder(); };
        validate.Click += delegate { ValidateAndReport(); };
        approve.Click += delegate { ApproveCurrent(); };
        save.Click += delegate { SaveCurrent(); };
        toolbar.Items.Add(open);
        toolbar.Items.Add(reload);
        toolbar.Items.Add(new ToolStripSeparator());
        toolbar.Items.Add(validate);
        toolbar.Items.Add(approve);
        toolbar.Items.Add(save);
        Controls.Add(toolbar);

        folderLabel.Dock = DockStyle.Top;
        folderLabel.Height = 28;
        folderLabel.Padding = new Padding(8, 6, 8, 0);
        folderLabel.ForeColor = Color.DimGray;
        Controls.Add(folderLabel);
        folderLabel.BringToFront();
        toolbar.BringToFront();

        SplitContainer outer = new SplitContainer();
        outer.Dock = DockStyle.Fill;
        outer.SplitterDistance = 285;
        outer.Panel1.Padding = new Padding(6);
        outer.Panel2.Padding = new Padding(6);
        Controls.Add(outer);
        outer.BringToFront();
        folderLabel.BringToFront();
        toolbar.BringToFront();

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

        TableLayoutPanel right = new TableLayoutPanel();
        right.Dock = DockStyle.Fill;
        right.ColumnCount = 1;
        right.RowCount = 4;
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 34));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 66));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        outer.Panel2.Controls.Add(right);

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

        statusLabel.Dock = DockStyle.Fill;
        statusLabel.Padding = new Padding(4, 5, 4, 0);
        statusLabel.ForeColor = Color.DimGray;
        right.Controls.Add(statusLabel, 0, 3);

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
        resultGrid.Columns.Add(ReadOnlyTextColumn("Value", 65));
        DataGridViewComboBoxColumn outcome = new DataGridViewComboBoxColumn();
        outcome.HeaderText = "Outcome";
        outcome.Width = 95;
        outcome.Items.AddRange("success", "failure", "pending", "info");
        resultGrid.Columns.Add(outcome);
        DataGridViewTextBoxColumn message = ReadOnlyTextColumn("Message", 330);
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

    private static DataGridViewTextBoxColumn ReadOnlyTextColumn(string title, int width)
    {
        DataGridViewTextBoxColumn column = new DataGridViewTextBoxColumn();
        column.HeaderText = title;
        column.Width = width;
        column.ReadOnly = true;
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
                IDictionary<string, object> root = serializer.DeserializeObject(text)
                    as IDictionary<string, object>;
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
        if (profileList.Items.Count > 0) profileList.SelectedIndex = selectedIndex;
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
                TreeNode procedureNode = new TreeNode(readingName + " / " + procedureName);
                IList inputs = AsList(Get(procedure, "inputs"));
                TreeNode inputsNode = new TreeNode("Inputs (" + (inputs == null ? 0 : inputs.Count) + ")");
                if (inputs != null)
                {
                    foreach (object inputObject in inputs)
                    {
                        IDictionary<string, object> input = AsDict(inputObject);
                        if (input == null) continue;
                        inputsNode.Nodes.Add(String.Format("{0}: {1}..{2} {3}",
                            Get(input, "id"), Get(input, "minimum"), Get(input, "maximum"),
                            Get(input, "unit")));
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
                                stepNode.Nodes.Add(String.Format("Alternative {0}: {1} @ {2}",
                                    alternativeNumber, type, address));
                                AddResultRows(procedureName, stepNumber, alternative);
                            }
                        }
                        procedureNode.Nodes.Add(stepNode);
                    }
                }
                procedureTree.Nodes.Add(procedureNode);
            }
        }
        procedureTree.ExpandAll();
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
            resultGrid.Rows[rowIndex].Tag = mapping;
        }
    }

    private void ResultCellValueChanged(object sender, DataGridViewCellEventArgs e)
    {
        if (loading || e.RowIndex < 0 || e.ColumnIndex != 4) return;
        IDictionary<string, object> mapping = resultGrid.Rows[e.RowIndex].Tag
            as IDictionary<string, object>;
        if (mapping == null) return;
        mapping["outcome"] = Convert.ToString(resultGrid.Rows[e.RowIndex].Cells[4].Value);
        MarkDirty("Outcome changed; validate and approve before release");
    }

    private void ReleaseStatusChanged(object sender, EventArgs e)
    {
        if (loading || current == null) return;
        current.Root["release_status"] = Convert.ToString(releaseStatus.SelectedItem);
        MarkDirty("Release status changed");
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
                IList steps = AsList(Get(procedure, "steps"));
                if (steps == null || steps.Count == 0)
                    errors.Add("Procedure " + id + " has no steps");
                if (steps != null)
                {
                    foreach (object stepObject in steps)
                    {
                        IDictionary<string, object> step = AsDict(stepObject);
                        IList alternatives = step == null ? null : AsList(Get(step, "alternatives"));
                        if (alternatives == null || alternatives.Count == 0)
                        {
                            errors.Add("Procedure " + id + " has a step without alternatives");
                            continue;
                        }
                        foreach (object alternativeObject in alternatives)
                        {
                            IDictionary<string, object> alternative = AsDict(alternativeObject);
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
        dirty = false;
        UpdateDirtyUi();
    }

    private static object Get(IDictionary<string, object> dictionary, string key)
    {
        if (dictionary == null) return null;
        object value;
        return dictionary.TryGetValue(key, out value) ? value : null;
    }

    private static IDictionary<string, object> AsDict(object value)
    {
        return value as IDictionary<string, object>;
    }

    private static IList AsList(object value)
    {
        return value as IList;
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
