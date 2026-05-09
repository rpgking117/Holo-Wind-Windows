using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

namespace MorrowindPipBoySetup
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            if (!IsAdmin())
            {
                try { Process.Start(new ProcessStartInfo(Application.ExecutablePath) { Verb = "runas" }); }
                catch { }
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.ThreadException += (s, e) => LogCrash(e.Exception);
            AppDomain.CurrentDomain.UnhandledException += (s, e) => LogCrash(e.ExceptionObject as Exception);
            try { Application.Run(new SetupForm()); }
            catch (Exception ex) { LogCrash(ex); }
        }

        static bool IsAdmin()
        {
            return new WindowsPrincipal(WindowsIdentity.GetCurrent())
                       .IsInRole(WindowsBuiltInRole.Administrator);
        }

        static void LogCrash(Exception ex)
        {
            if (ex == null) return;
            try
            {
                string log = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "setup_crash.log");
                File.WriteAllText(log, ex.ToString());
                MessageBox.Show("Setup crashed:\n\n" + ex.Message + "\n\nDetails written to:\n" + log,
                    "Morrowind: PipBoy Edition Setup Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch { }
        }
    }

    class SetupForm : Form
    {
        Panel pgWelcome, pgPaths, pgInstall, pgDone;
        Panel mainContent;
        int currentPage = 0;

        TextBox txtFO4, txtMorrowind, txtOpenMW;

        RichTextBox rtbLog;
        ProgressBar pbProgress;
        Label lblStatus;

        Button btnBack, btnNext, btnCancel;

        string _tempDir = null;

        static readonly Color HEADER_BG  = Color.White;
        static readonly Color RULE_COLOR  = Color.FromArgb(210, 210, 210);
        static readonly Color FOOTER_BG   = SystemColors.Control;
        static readonly Color TITLE_COLOR = Color.FromArgb(30, 30, 30);
        static readonly Color SUB_COLOR   = Color.FromArgb(80, 80, 80);
        static readonly Color LABEL_COLOR = Color.FromArgb(50, 50, 50);

        const string PRODUCT = "Morrowind: PipBoy Edition";
        const string VERSION = "v1.0";

        public SetupForm()
        {
            Text            = PRODUCT + " — Setup  " + VERSION;
            ClientSize      = new Size(540, 440);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox     = false;
            MinimizeBox     = false;
            StartPosition   = FormStartPosition.CenterScreen;
            BackColor       = Color.White;
            Font            = new Font("Segoe UI", 9F);

            BuildHeader();
            BuildRule(DockStyle.Top);
            BuildMainContent();
            BuildRule(DockStyle.Bottom);
            BuildFooter();

            BuildWelcomePage();
            BuildPathsPage();
            BuildInstallPage();
            BuildDonePage();

            ShowPage(0);

            FormClosed += (s, e) => Cleanup();
        }

        void Cleanup()
        {
            if (_tempDir != null && Directory.Exists(_tempDir))
            {
                try { Directory.Delete(_tempDir, true); } catch { }
                _tempDir = null;
            }
        }

        // ---- Steam registry detection ----------------------------------------
        static string FindSteamGame(string name)
        {
            string steam = null;
            foreach (string key in new[] {
                @"SOFTWARE\WOW6432Node\Valve\Steam",
                @"SOFTWARE\Valve\Steam" })
            {
                try
                {
                    using (var k = Registry.LocalMachine.OpenSubKey(key))
                        if (k != null) steam = k.GetValue("InstallPath") as string;
                    if (steam != null) break;
                }
                catch { }
            }
            if (steam == null) return "";

            var libs = new System.Collections.Generic.List<string> { steam };
            string vdf = Path.Combine(steam, @"steamapps\libraryfolders.vdf");
            if (File.Exists(vdf))
            {
                foreach (string line in File.ReadAllLines(vdf))
                {
                    var m = Regex.Match(line, "\"path\"\\s+\"([^\"]+)\"");
                    if (m.Success) libs.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
                }
            }

            foreach (string lib in libs)
            {
                string p = Path.Combine(lib, @"steamapps\common", name);
                if (Directory.Exists(p)) return p;
            }
            return "";
        }

        // ---- Header ----------------------------------------------------------
        void BuildHeader()
        {
            var hdr = new Panel { Dock = DockStyle.Top, Height = 72, BackColor = HEADER_BG };
            hdr.Paint += (s, e) =>
            {
                Graphics g = e.Graphics;
                g.Clear(HEADER_BG);
                using (var f = new Font("Segoe UI", 14f, FontStyle.Bold))
                    g.DrawString(PRODUCT, f, new SolidBrush(TITLE_COLOR), 18f, 12f);
                using (var f = new Font("Segoe UI", 9f))
                    g.DrawString("Morrowind inside the Fallout 4 Pip-Boy  " + VERSION,
                        f, new SolidBrush(SUB_COLOR), 20f, 44f);
            };
            Controls.Add(hdr);
        }

        void BuildRule(DockStyle dock)
        {
            Controls.Add(new Panel { Dock = dock, Height = 1, BackColor = RULE_COLOR });
        }

        void BuildMainContent()
        {
            mainContent = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Padding = new Padding(18, 10, 18, 10) };
            Controls.Add(mainContent);
        }

        // ---- Footer ----------------------------------------------------------
        void BuildFooter()
        {
            var foot = new Panel { Dock = DockStyle.Bottom, Height = 46, BackColor = FOOTER_BG };

            btnBack   = SysBtn("< Back",  foot);
            btnNext   = SysBtn("Next >",  foot);
            btnCancel = SysBtn("Cancel",  foot);

            foot.Resize += (s, e) =>
            {
                btnCancel.Location = new Point(foot.Width - btnCancel.Width - 10, 9);
                btnNext.Location   = new Point(btnCancel.Left - btnNext.Width - 5, 9);
                btnBack.Location   = new Point(btnNext.Left   - btnBack.Width - 2, 9);
            };

            btnBack.Click   += (s, e) => Navigate(-1);
            btnNext.Click   += (s, e) => Navigate(+1);
            btnCancel.Click += (s, e) =>
            {
                if (currentPage >= 3) { Close(); return; }
                if (MessageBox.Show("Cancel installation?", PRODUCT + " Setup",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
                    Close();
            };

            Controls.Add(foot);
        }

        static Button SysBtn(string text, Panel parent)
        {
            var b = new Button { Text = text, Size = new Size(80, 26), FlatStyle = FlatStyle.System };
            parent.Controls.Add(b);
            return b;
        }

        // ---- Page 1: Welcome -------------------------------------------------
        void BuildWelcomePage()
        {
            pgWelcome = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgWelcome.Controls.Add(SectionLabel("Welcome", ref y));
            pgWelcome.Controls.Add(InfoBox(
                "This installer will set up Morrowind: PipBoy Edition, which renders " +
                "The Elder Scrolls III: Morrowind live inside the Fallout 4 Pip-Boy screen, " +
                "powered by a modified build of OpenMW 0.50.\r\n\r\n" +
                "Steam must be installed with both Fallout 4 and Morrowind. " +
                "OpenMW 0.50 must be installed separately.",
                ref y, 90));

            y += 8;

            var f4Lbl = new Label {
                Text = "Required: F4SE (Fallout 4 Script Extender)",
                Font = new Font("Segoe UI", 9f, FontStyle.Bold),
                ForeColor = Color.FromArgb(150, 100, 0),
                AutoSize = false, Size = new Size(490, 20), Location = new Point(0, y), BackColor = Color.White
            };
            pgWelcome.Controls.Add(f4Lbl);
            y += 22;

            var f4Link = new LinkLabel {
                Text = "Download F4SE: nexusmods.com/fallout4/mods/42147",
                Font = new Font("Segoe UI", 9f),
                AutoSize = false, Size = new Size(490, 18), Location = new Point(0, y)
            };
            f4Link.LinkClicked += (s, e) => OpenUrl("https://www.nexusmods.com/fallout4/mods/42147");
            pgWelcome.Controls.Add(f4Link);
            y += 28;

            pgWelcome.Controls.Add(InfoBox(
                "IMPORTANT: Back up your OpenMW settings folder before continuing.\r\n" +
                "This installer overwrites settings.cfg, input_v3.xml, and shaders.yaml\r\n" +
                "in %USERPROFILE%\\Documents\\My Games\\OpenMW\\",
                ref y, 52));

            mainContent.Controls.Add(pgWelcome);
        }

        // ---- Page 2: Paths ---------------------------------------------------
        void BuildPathsPage()
        {
            pgPaths = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgPaths.Controls.Add(SectionLabel("Installation Paths", ref y));
            pgPaths.Controls.Add(InfoBox(
                "Fallout 4 and Morrowind were detected from your Steam installation. " +
                "Browse to correct any path. OpenMW must be located manually.",
                ref y, 40));

            y += 8;

            pgPaths.Controls.Add(FieldLabel("Fallout 4 install folder:", y)); y += 20;
            txtFO4 = PathRow(pgPaths, y, "Fallout 4", "Fallout4.exe"); y += 32;

            pgPaths.Controls.Add(FieldLabel("Morrowind install folder:", y)); y += 20;
            txtMorrowind = PathRow(pgPaths, y, "Morrowind", @"Data Files\Morrowind.esm"); y += 32;

            pgPaths.Controls.Add(FieldLabel("OpenMW 0.50 install folder (containing openmw.exe):", y)); y += 20;
            txtOpenMW = PathRow(pgPaths, y, null, "openmw.exe"); y += 32;

            pgPaths.Controls.Add(InfoBox(
                "Note: Paths containing spaces may cause issues. " +
                "If possible, install games to paths without spaces.",
                ref y, 32));

            mainContent.Controls.Add(pgPaths);
        }

        TextBox PathRow(Panel parent, int y, string steamGameName, string validateFile)
        {
            var tb = new TextBox {
                Location = new Point(0, y), Size = new Size(400, 22),
                Font = new Font("Segoe UI", 9f),
                Text = steamGameName != null ? FindSteamGame(steamGameName) : ""
            };
            var btn = new Button {
                Text = "Browse...", Location = new Point(406, y - 1),
                Size = new Size(78, 24), FlatStyle = FlatStyle.System
            };
            string validate = validateFile;
            btn.Click += (s, e) =>
            {
                using (var dlg = new FolderBrowserDialog())
                {
                    dlg.SelectedPath = tb.Text;
                    dlg.ShowNewFolderButton = false;
                    if (dlg.ShowDialog() == DialogResult.OK)
                    {
                        if (!File.Exists(Path.Combine(dlg.SelectedPath, validate)))
                            MessageBox.Show(validate + " was not found in that folder.",
                                PRODUCT + " Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        tb.Text = dlg.SelectedPath;
                    }
                }
            };
            parent.Controls.Add(tb);
            parent.Controls.Add(btn);
            return tb;
        }

        // ---- Page 3: Installing ----------------------------------------------
        void BuildInstallPage()
        {
            pgInstall = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgInstall.Controls.Add(SectionLabel("Installing", ref y));

            lblStatus = new Label {
                Text = "Starting installation...",
                Font = new Font("Segoe UI", 9f), ForeColor = LABEL_COLOR,
                AutoSize = false, Size = new Size(490, 18), Location = new Point(0, y), BackColor = Color.White
            };
            pgInstall.Controls.Add(lblStatus);
            y += 24;

            rtbLog = new RichTextBox {
                Location = new Point(0, y), Size = new Size(490, 230),
                BackColor = Color.FromArgb(245, 245, 245), ForeColor = Color.FromArgb(30, 30, 30),
                Font = new Font("Consolas", 8f), ReadOnly = true,
                BorderStyle = BorderStyle.FixedSingle, ScrollBars = RichTextBoxScrollBars.Vertical
            };
            pgInstall.Controls.Add(rtbLog);
            y += 236;

            pbProgress = new ProgressBar {
                Location = new Point(0, y), Size = new Size(490, 16),
                Style = ProgressBarStyle.Marquee, MarqueeAnimationSpeed = 20
            };
            pgInstall.Controls.Add(pbProgress);

            mainContent.Controls.Add(pgInstall);
        }

        // ---- Page 4: Done ----------------------------------------------------
        void BuildDonePage()
        {
            pgDone = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgDone.Controls.Add(SectionLabel("Installation Complete", ref y));
            pgDone.Controls.Add(InfoBox("Morrowind: PipBoy Edition has been installed successfully.", ref y, 28));
            y += 8;

            pgDone.Controls.Add(FieldLabel("How to play:", y)); y += 22;
            pgDone.Controls.Add(InfoBox(
                "1.  Launch Fallout 4 via f4se_loader.exe (NOT the normal launcher).\r\n" +
                "2.  Load a save — use the console: help morrowind 4, then player.additem XX000800.\r\n" +
                "3.  Open your Pip-Boy, go to MISC, and insert the Morrowind Holotape.\r\n" +
                "4.  Morrowind launches on the Pip-Boy screen.\r\n" +
                "5.  Press Tab to close Morrowind and return to Fallout 4.",
                ref y, 100));

            mainContent.Controls.Add(pgDone);
        }

        // ---- Navigation ------------------------------------------------------
        void ShowPage(int page)
        {
            currentPage = page;
            pgWelcome.Visible = (page == 0);
            pgPaths.Visible   = (page == 1);
            pgInstall.Visible = (page == 2);
            pgDone.Visible    = (page == 3);

            btnBack.Enabled   = (page == 1);
            btnBack.Visible   = (page <= 1);
            btnCancel.Visible = (page <= 2);
            btnNext.Visible   = true;

            switch (page)
            {
                case 0: btnNext.Text = "Next >";        btnNext.Enabled = true;  break;
                case 1: btnNext.Text = "Install";       btnNext.Enabled = true;  break;
                case 2: btnNext.Text = "Installing..."; btnNext.Enabled = false; break;
                case 3: btnNext.Text = "Finish";        btnNext.Enabled = true;
                        btnBack.Visible = false; btnCancel.Visible = false; break;
            }
        }

        void Navigate(int dir)
        {
            if (dir == +1)
            {
                if (currentPage == 0) { ShowPage(1); return; }
                if (currentPage == 1) { if (ValidatePaths()) { ShowPage(2); RunInstall(); } return; }
                if (currentPage == 3) { Close(); return; }
            }
            if (dir == -1 && currentPage == 1) ShowPage(0);
        }

        bool ValidatePaths()
        {
            if (!File.Exists(Path.Combine(txtFO4.Text.Trim(), "Fallout4.exe")))
            {
                MessageBox.Show("Fallout4.exe was not found in the specified folder.",
                    PRODUCT + " Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            if (!File.Exists(Path.Combine(txtMorrowind.Text.Trim(), @"Data Files\Morrowind.esm")))
            {
                MessageBox.Show("Morrowind.esm was not found in the specified folder.",
                    PRODUCT + " Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            if (!File.Exists(Path.Combine(txtOpenMW.Text.Trim(), "openmw.exe")))
            {
                MessageBox.Show("openmw.exe was not found in the specified folder.\nBrowse to your OpenMW 0.50 installation directory.",
                    PRODUCT + " Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            return true;
        }

        // ---- Installation runner ---------------------------------------------
        void RunInstall()
        {
            // Extract the embedded PS1 to a temp directory so PowerShell can run it.
            // InstallerDir is passed so the PS1 can find the build\ folder next to the exe.
            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            _tempDir = Path.Combine(Path.GetTempPath(), "MorrowindPipBoySetup_" + Path.GetRandomFileName());
            Directory.CreateDirectory(_tempDir);
            string ps1 = Path.Combine(_tempDir, "MorrowindPipBoyEdition_Setup.ps1");

            try
            {
                using (var stream = Assembly.GetExecutingAssembly()
                           .GetManifestResourceStream("MorrowindPipBoyEdition_Setup.ps1"))
                {
                    if (stream == null)
                        throw new Exception("Embedded installer script not found in this executable.");
                    using (var reader = new StreamReader(stream, System.Text.Encoding.UTF8))
                        File.WriteAllText(ps1, reader.ReadToEnd(), System.Text.Encoding.UTF8);
                }
            }
            catch (Exception ex)
            {
                LogLine("ERROR: " + ex.Message, Color.FromArgb(180, 0, 0));
                OnFail(); return;
            }

            string fo4     = txtFO4.Text.Trim()      .Replace("'", "''");
            string mw      = txtMorrowind.Text.Trim() .Replace("'", "''");
            string omw     = txtOpenMW.Text.Trim()    .Replace("'", "''");
            string instDir = exeDir                    .Replace("'", "''");
            string ps1Esc  = ps1                       .Replace("'", "''");

            string args = string.Format(
                "-NoProfile -NonInteractive -ExecutionPolicy Bypass " +
                "-Command \"$ErrorActionPreference='Continue'; & '{0}' " +
                "-FO4Path '{1}' -MorrowindPath '{2}' -OpenMWPath '{3}' -InstallerDir '{4}' *>&1\"",
                ps1Esc, fo4, mw, omw, instDir);

            var psi = new ProcessStartInfo("powershell.exe")
            {
                Arguments              = args,
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                CreateNoWindow         = true,
                WorkingDirectory       = exeDir,
                StandardOutputEncoding = System.Text.Encoding.UTF8
            };

            var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
            proc.OutputDataReceived += (_, ev) => { if (ev.Data != null) SafeLog(ev.Data); };
            proc.ErrorDataReceived  += (_, ev) => { if (ev.Data != null) SafeLog(ev.Data); };
            proc.Exited += (_, __) =>
            {
                bool ok = proc.ExitCode == 0;
                BeginInvoke((Action)(() => { if (ok) ShowPage(3); else OnFail(); }));
            };

            proc.Start();
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
        }

        static readonly Regex RxAnsi = new Regex(@"\x1b(\[[^@-~]*[@-~]|\][^\x07]*\x07)", RegexOptions.Compiled);

        void SafeLog(string raw)
        {
            if (InvokeRequired) { BeginInvoke((Action<string>)SafeLog, raw); return; }
            string clean = RxAnsi.Replace(raw, "").Trim();
            if (clean.Length == 0) return;
            LogLine(clean, PickColor(clean));
            string t = clean.TrimStart();
            if (t.StartsWith("+--[") || (t.StartsWith("[") && t.Length > 2))
            {
                int c = t.IndexOf(']');
                if (c > 0) lblStatus.Text = t.Substring(0, c + 1).Trim() + "  " + t.Substring(c + 1).Trim();
            }
        }

        void LogLine(string text, Color color)
        {
            rtbLog.SelectionStart  = rtbLog.TextLength;
            rtbLog.SelectionLength = 0;
            rtbLog.SelectionColor  = color;
            rtbLog.AppendText(text + "\n");
            rtbLog.ScrollToCaret();
        }

        static Color PickColor(string line)
        {
            string u = line.ToUpperInvariant();
            if (u.Contains("[+]") || u.Contains("OK"))   return Color.FromArgb(0, 130, 0);
            if (u.Contains("[!]") || u.Contains("WARN")) return Color.FromArgb(160, 100, 0);
            if (u.Contains("[X]") || u.Contains("FAIL") || u.Contains("ERROR")) return Color.FromArgb(180, 0, 0);
            if (line.TrimStart().StartsWith("+--["))      return Color.FromArgb(0, 70, 160);
            return Color.FromArgb(40, 40, 40);
        }

        void OnFail()
        {
            pbProgress.Style  = ProgressBarStyle.Continuous;
            pbProgress.Value  = 0;
            lblStatus.Text    = "Installation failed — see the log above.";
            btnNext.Text      = "Close";
            btnNext.Enabled   = true;
            btnCancel.Enabled = false;
        }

        static void OpenUrl(string url) { try { Process.Start(url); } catch { } }

        static Label SectionLabel(string text, ref int y)
        {
            var l = new Label {
                Text = text, Font = new Font("Segoe UI", 11f, FontStyle.Bold),
                ForeColor = TITLE_COLOR, AutoSize = false,
                Size = new Size(490, 26), Location = new Point(0, y), BackColor = Color.White
            };
            y += 28;
            return l;
        }

        static Label FieldLabel(string text, int y)
        {
            return new Label {
                Text = text, Font = new Font("Segoe UI", 9f), ForeColor = LABEL_COLOR,
                AutoSize = false, Size = new Size(490, 18), Location = new Point(0, y), BackColor = Color.White
            };
        }

        static RichTextBox InfoBox(string text, ref int y, int height)
        {
            var rtb = new RichTextBox {
                Text = text, Font = new Font("Segoe UI", 9f),
                ForeColor = SUB_COLOR, BackColor = Color.White, ReadOnly = true,
                BorderStyle = BorderStyle.None, ScrollBars = RichTextBoxScrollBars.None,
                Location = new Point(0, y), Size = new Size(490, height)
            };
            y += height;
            return rtb;
        }
    }
}
