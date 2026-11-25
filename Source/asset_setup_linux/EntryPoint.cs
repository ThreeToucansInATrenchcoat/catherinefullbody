using System.ComponentModel;
using System.Diagnostics;

namespace Catherine.AssetSetup;

static class EntryPoint
{
    static int Main(string[] args)
    {
        var root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
        string? requestedPack = null;
        var sources = new List<string>();
        bool headless = false, scanOnly = false, nativeProgress = false;
        LinuxSetupDialogs? dialogs = null;
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancellation.Cancel(); };
        try
        {
            if (args.Length == 4 && args[0] == "--translate")
            {
                headless = true;
                SpirvSanitizer.PrepareTools();
                var (binary, resources, _) = Shaders.Translate(File.ReadAllBytes(args[1]), File.ReadAllBytes(args[2]));
                File.WriteAllBytes(args[3] + ".spv", binary);
                File.WriteAllText(args[3] + ".resources", resources);
                return 0;
            }
            for (int i = 0; i < args.Length; i++)
                switch (args[i])
                {
                    case "--root" when i + 1 < args.Length:
                        root = Path.GetFullPath(args[++i]); break;
                    case "--pack" when i + 1 < args.Length:
                        requestedPack = Path.GetFullPath(args[++i]); break;
                    case "--scan" when i + 1 < args.Length:
                        sources.Add(Path.GetFullPath(args[++i])); break;
                    case "--headless":
                        headless = true; break;
                    case "--native-progress":
                        nativeProgress = true; break;
                    case "--scan-only":
                        scanOnly = true; break;
                    default:
                        throw new ArgumentException("Usage: CatherineAssetSetup [--root DIR] [--pack FILE] [--scan DIR]... [--headless] [--scan-only]");
                }

            var pack = requestedPack ?? Path.Combine(root, "assets.pak");
            using var setupLock = new FileStream(pack + ".setup-lock", FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            dialogs = new LinuxSetupDialogs(root, headless, nativeProgress, cancellation);
            InstallResource(Path.Combine(root, "assets-required.json"), "Catherine.AssetSetup.Manifest");
            string lastStage = "";
            long lastUpdate = 0;
            void Report(ProgressState progress)
            {
                long now = Environment.TickCount64;
                if (progress.Stage == lastStage && now - lastUpdate < 500 && progress.Fraction < 1) return;
                lastStage = progress.Stage;
                lastUpdate = now;
                dialogs.Report(progress);
            }
            void Log(string text)
            {
                File.AppendAllText(Path.Combine(root, "assets-setup.log"), $"{DateTime.UtcNow:O} {text}\n");
                Console.Error.WriteLine(text);
            }

            new Installer(root, pack, headless, Report, dialogs.Select, Log, cancellation.Token).Run(sources, scanOnly);
            return 0;
        }
        catch (OperationCanceledException) { return 2; }
        catch (Exception e)
        {
            string log = Path.Combine(root, "assets-errors.log");
            try { File.AppendAllText(log, $"{DateTime.UtcNow:O} {e}\n"); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            string message = $"{e.GetBaseException().Message}\n\nDetails: {log}";
            Console.Error.WriteLine(message);
            dialogs ??= new LinuxSetupDialogs(root, headless, nativeProgress, cancellation);
            return dialogs.Error(message) || headless ? 1 : 3;
        }
    }

    internal static void InstallResource(string path, string resource)
    {
        if (File.Exists(path)) return;
        string temporary = Path.Combine(Path.GetDirectoryName(path)!, Path.GetRandomFileName());
        try
        {
            using var manifest = typeof(EntryPoint).Assembly.GetManifestResourceStream(resource)!;
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write))
                manifest.CopyTo(output);
            try { File.Move(temporary, path); }
            catch (IOException) when (File.Exists(path)) { }
        }
        finally { File.Delete(temporary); }
    }

}

sealed class LinuxSetupDialogs
{
    readonly bool kde;
    readonly string? picker;
    readonly string icon;
    readonly CancellationTokenSource cancellation;
    readonly bool nativeProgress;

    public LinuxSetupDialogs(string root, bool headless, bool nativeProgress, CancellationTokenSource cancellation)
    {
        this.cancellation = cancellation;
        this.nativeProgress = nativeProgress;
        icon = Path.Combine(root, "catherine.png");
        if (headless || (Environment.GetEnvironmentVariable("DISPLAY") == null &&
                         Environment.GetEnvironmentVariable("WAYLAND_DISPLAY") == null)) return;
        kde = Available("kdialog");
        picker = kde ? "kdialog" : Available("zenity") ? "zenity" : null;
        if (picker != null && !File.Exists(icon))
        {
            var work = Path.Combine(root, "assets-setup-work");
            icon = Path.Combine(work, "catherine.ico");
            try
            {
                Directory.CreateDirectory(work);
                EntryPoint.InstallResource(icon, "Catherine.AssetSetup.GameIcon");
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    static bool Available(string command) =>
        (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator)
            .Any(dir => dir.Length > 0 && File.Exists(Path.Combine(dir, command)));

    (int Status, string Output) Run(string command, params string[] args)
    {
        var start = new ProcessStartInfo(command) { UseShellExecute = false, RedirectStandardOutput = true };
        foreach (string arg in args) start.ArgumentList.Add(arg);
        try
        {
            using var process = Process.Start(start)!;
            using var registration = cancellation.Token.Register(() =>
            {
                try { if (!process.HasExited) process.Kill(); }
                catch (InvalidOperationException) { }
                catch (Win32Exception) { }
            });
            string output = process.StandardOutput.ReadToEnd();
            process.WaitForExit();
            cancellation.Token.ThrowIfCancellationRequested();
            return (process.ExitCode, output.TrimEnd('\r', '\n'));
        }
        catch (Win32Exception) { return (-1, ""); }
    }

    public string? Select(bool update)
    {
        string title = update ? "Select the extracted 1.0.1 update" : "Select the extracted Switch game files";
        if (picker == null)
        {
            if (Console.IsInputRedirected)
                throw new InvalidOperationException("Install kdialog or zenity, or run the asset installer from a terminal with --scan DIR.");
            Console.Error.WriteLine(title + " (empty to cancel):");
            return Console.ReadLine() is { Length: > 0 } path ? path : null;
        }
        Send(-1, update ? "Update files required" : "Game files required", title);
        var result = kde
            ? Run(picker, "--icon", icon, "--title", title, "--getexistingdirectory", Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))
            : Run(picker, "--file-selection", "--directory", "--title=" + title);
        return result.Status == 0 && result.Output.Length != 0 ? result.Output : null;
    }

    void Send(int percent, string stage, string detail)
    {
        if (!nativeProgress) return;
        static string Clean(string value) => value.Replace('\r', ' ').Replace('\n', ' ').Replace('\t', ' ');
        Console.Out.WriteLine($"P\t{percent}\t{Clean(stage)}\t{Clean(detail)}");
        Console.Out.Flush();
    }

    public void Report(ProgressState progress)
    {
        Console.Error.WriteLine($"{progress.Stage}: {progress.Detail}");
        Send((int)(progress.Fraction * 100), progress.Stage, progress.Detail);
    }

    public bool Error(string message)
    {
        if (kde)
            return Run("kdialog", "--icon", icon, "--title", "Catherine asset setup failed", "--error", message).Status == 0;
        return picker != null &&
            Run(picker, "--error", "--title=Catherine asset setup failed", "--text=" + message).Status == 0;
    }

}
