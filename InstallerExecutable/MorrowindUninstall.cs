using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace MorrowindPipBoyUninstall
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
            try { Application.Run(new UninstallForm()); }
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
                string log = System.IO.Path.Combine(
                    System.IO.Path.GetDirectoryName(Application.ExecutablePath),
                    "uninstall_crash.log");
                System.IO.File.WriteAllText(log, ex.ToString());
                MessageBox.Show("Uninstaller crashed:\n\n" + ex.Message + "\n\nDetails written to:\n" + log,
                    "Morrowind: PipBoy Edition Uninstaller", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch { }
        }
    }

    class UninstallForm : Form
    {
        Panel pgConfirm, pgUninstall, pgDone;
        Panel mainContent;
        int currentPage = 0;

        RichTextBox rtbLog;
        ProgressBar pbProgress;
        Label lblStatus;

        Button btnUninstall, btnCancel, btnClose;

        string _tempDir = null;

        static readonly Color HEADER_BG  = Color.White;
        static readonly Color RULE_COLOR  = Color.FromArgb(210, 210, 210);
        static readonly Color FOOTER_BG   = SystemColors.Control;
        static readonly Color TITLE_COLOR = Color.FromArgb(30, 30, 30);
        static readonly Color SUB_COLOR   = Color.FromArgb(80, 80, 80);
        static readonly Color LABEL_COLOR = Color.FromArgb(50, 50, 50);

        const string PRODUCT = "Morrowind: PipBoy Edition";
        const string VERSION = "v1.0";

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= 0x02000000; // WS_EX_COMPOSITED — single-pass paint, eliminates artifacts
                return cp;
            }
        }

        public UninstallForm()
        {
            SuspendLayout();
            Text            = PRODUCT + " — Uninstaller  " + VERSION;
            ClientSize      = new Size(540, 440);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox     = false;
            MinimizeBox     = false;
            StartPosition   = FormStartPosition.CenterScreen;
            BackColor       = Color.White;
            Font            = new Font("Segoe UI", 9F);
            DoubleBuffered  = true;

            BuildHeader();
            BuildRule(DockStyle.Top);
            BuildMainContent();
            BuildRule(DockStyle.Bottom);
            BuildFooter();

            BuildConfirmPage();
            BuildUninstallPage();
            BuildDonePage();

            ShowPage(0);

            FormClosed += (s, e) => Cleanup();
            ResumeLayout(true);
        }

        void Cleanup()
        {
            if (_tempDir != null && Directory.Exists(_tempDir))
            {
                try { Directory.Delete(_tempDir, true); } catch { }
                _tempDir = null;
            }
        }

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
                    g.DrawString("Uninstaller  " + VERSION, f, new SolidBrush(SUB_COLOR), 20f, 44f);
            };
            Controls.Add(hdr);
        }

        void BuildRule(DockStyle dock)
        {
            Controls.Add(new Panel { Dock = dock, Height = 1, BackColor = RULE_COLOR });
        }

        void BuildMainContent()
        {
            mainContent = new Panel
            {
                Dock = DockStyle.Fill,
                BackColor = Color.White,
                Padding = new Padding(18, 10, 18, 10)
            };
            Controls.Add(mainContent);
        }

        void BuildFooter()
        {
            var foot = new Panel { Dock = DockStyle.Bottom, Height = 46, BackColor = FOOTER_BG };

            btnUninstall = SysBtn("Uninstall", foot);
            btnCancel    = SysBtn("Cancel",    foot);
            btnClose     = SysBtn("Close",     foot);

            foot.Resize += (s, e) =>
            {
                btnClose.Location     = new Point(foot.Width - btnClose.Width - 10, 9);
                btnCancel.Location    = new Point(foot.Width - btnCancel.Width - 10, 9);
                btnUninstall.Location = new Point(btnCancel.Left - btnUninstall.Width - 5, 9);
            };

            btnUninstall.Click += (s, e) => { ShowPage(1); RunUninstall(); };
            btnCancel.Click    += (s, e) => Close();
            btnClose.Click     += (s, e) => Close();

            Controls.Add(foot);
        }

        static Button SysBtn(string text, Panel parent)
        {
            var b = new Button { Text = text, Size = new Size(80, 26), FlatStyle = FlatStyle.System };
            parent.Controls.Add(b);
            return b;
        }

        void BuildConfirmPage()
        {
            pgConfirm = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgConfirm.Controls.Add(SectionLabel("Uninstall Morrowind: PipBoy Edition", ref y));
            pgConfirm.Controls.Add(InfoBox(
                "This will remove all installed Morrowind: PipBoy Edition files, including:\r\n\r\n" +
                "  •  SDL2 capture proxy from your OpenMW folder\r\n" +
                "  •  F4SE plugin and Fallout 4 mod files\r\n" +
                "  •  OpenMW configuration files and combat script patch\r\n" +
                "  •  launch_openmw.exe and morrowind_watcher.ps1\r\n\r\n" +
                "Your Morrowind and Fallout 4 save files will not be affected.",
                ref y, 148));

            y += 8;
            pgConfirm.Controls.Add(InfoBox(
                "Click Uninstall to proceed, or Cancel to exit.",
                ref y, 22));

            mainContent.Controls.Add(pgConfirm);
        }

        void BuildUninstallPage()
        {
            pgUninstall = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgUninstall.Controls.Add(SectionLabel("Uninstalling", ref y));

            lblStatus = new Label
            {
                Text = "Removing installed files...",
                Font = new Font("Segoe UI", 9f),
                ForeColor = LABEL_COLOR,
                AutoSize = false,
                Size = new Size(490, 18),
                Location = new Point(0, y),
                BackColor = Color.White
            };
            pgUninstall.Controls.Add(lblStatus);
            y += 24;

            rtbLog = new RichTextBox
            {
                Location = new Point(0, y), Size = new Size(490, 240),
                BackColor = Color.FromArgb(245, 245, 245), ForeColor = Color.FromArgb(30, 30, 30),
                Font = new Font("Consolas", 8f), ReadOnly = true,
                BorderStyle = BorderStyle.FixedSingle, ScrollBars = RichTextBoxScrollBars.Vertical
            };
            pgUninstall.Controls.Add(rtbLog);
            y += 246;

            pbProgress = new ProgressBar
            {
                Location = new Point(0, y), Size = new Size(490, 16),
                Style = ProgressBarStyle.Marquee, MarqueeAnimationSpeed = 20
            };
            pgUninstall.Controls.Add(pbProgress);

            mainContent.Controls.Add(pgUninstall);
        }

        void BuildDonePage()
        {
            pgDone = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Visible = false };
            int y = 6;

            pgDone.Controls.Add(SectionLabel("Uninstall Complete", ref y));
            pgDone.Controls.Add(InfoBox(
                "Morrowind: PipBoy Edition has been removed.\r\n\r\n" +
                "OpenMW has been restored to its original SDL2.dll.\r\n" +
                "Your Morrowind and Fallout 4 game data are untouched.\r\n\r\n" +
                "You may now delete this uninstaller.",
                ref y, 80));

            mainContent.Controls.Add(pgDone);
        }

        void ShowPage(int page)
        {
            currentPage = page;
            pgConfirm.Visible   = (page == 0);
            pgUninstall.Visible = (page == 1);
            pgDone.Visible      = (page == 2);

            btnUninstall.Visible = (page == 0);
            btnCancel.Visible    = (page <= 1);
            btnClose.Visible     = (page == 2);

            if (page == 1)
            {
                btnCancel.Text    = "Uninstalling...";
                btnCancel.Enabled = false;
            }
        }

        void RunUninstall()
        {
            _tempDir = Path.Combine(Path.GetTempPath(),
                "MorrowindPipBoyUninstall_" + Path.GetRandomFileName());
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

            string ps1Esc = ps1.Replace("'", "''");
            string args = string.Format(
                "-NoProfile -NonInteractive -ExecutionPolicy Bypass " +
                "-Command \"$ErrorActionPreference='Continue'; & '{0}' -Uninstall *>&1\"",
                ps1Esc);

            var psi = new ProcessStartInfo("powershell.exe")
            {
                Arguments              = args,
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                CreateNoWindow         = true,
                StandardOutputEncoding = System.Text.Encoding.UTF8
            };

            var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
            proc.OutputDataReceived += (_, ev) => { if (ev.Data != null) SafeLog(ev.Data); };
            proc.ErrorDataReceived  += (_, ev) => { if (ev.Data != null) SafeLog(ev.Data); };
            proc.Exited += (_, __) =>
            {
                bool ok = proc.ExitCode == 0;
                BeginInvoke((Action)(() => { if (ok) ShowPage(2); else OnFail(); }));
            };

            proc.Start();
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
        }

        static readonly Regex RxAnsi = new Regex(
            @"\x1b(\[[^@-~]*[@-~]|\][^\x07]*\x07)", RegexOptions.Compiled);

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
            if (u.Contains("[+]") || u.Contains("OK"))                                  return Color.FromArgb(0, 130, 0);
            if (u.Contains("[!]") || u.Contains("WARN"))                                return Color.FromArgb(160, 100, 0);
            if (u.Contains("[X]") || u.Contains("FAIL") || u.Contains("ERROR"))        return Color.FromArgb(180, 0, 0);
            if (line.TrimStart().StartsWith("+--["))                                    return Color.FromArgb(0, 70, 160);
            return Color.FromArgb(40, 40, 40);
        }

        void OnFail()
        {
            pbProgress.Style  = ProgressBarStyle.Continuous;
            pbProgress.Value  = 0;
            lblStatus.Text    = "Uninstall failed — see the log above.";
            btnCancel.Text    = "Close";
            btnCancel.Enabled = true;
        }

        static Label SectionLabel(string text, ref int y)
        {
            var l = new Label
            {
                Text = text, Font = new Font("Segoe UI", 11f, FontStyle.Bold),
                ForeColor = TITLE_COLOR, AutoSize = false,
                Size = new Size(490, 26), Location = new Point(0, y), BackColor = Color.White
            };
            y += 28;
            return l;
        }

        static RichTextBox InfoBox(string text, ref int y, int height)
        {
            var rtb = new RichTextBox
            {
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
