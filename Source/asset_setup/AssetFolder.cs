using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Catherine.AssetSetup;

static class AssetStorageSettings
{
    public static bool FolderMode(string root)
    {
        string path = Path.Combine(root, "catherine-settings.ini"), section = "";
        bool value = false;
        if (!File.Exists(path)) return false;
        foreach (string raw in File.ReadLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] is ';' or '#') continue;
            if (line.StartsWith('[') && line.EndsWith(']')) { section = line[1..^1].Trim(); continue; }
            int equals = line.IndexOf('=');
            if (equals >= 0 && section.Equals("Catherine", StringComparison.OrdinalIgnoreCase) &&
                line[..equals].Trim().Equals("AltBuildMode", StringComparison.OrdinalIgnoreCase))
                value = line[(equals + 1)..].Trim().ToLowerInvariant() is "true" or "1";
        }
        return value;
    }
}

sealed class AssetFolder
{
    public const string LayoutName = ".catherine-layout.bin";
    public const string ReceiptName = ".catherine-folder.json";
    public string DirectoryPath { get; }
    public List<string> ExtraEntries { get; } = new();
    public List<MetadataSegment> Padding { get; } = new();
    public string ShaderPath => Path.Combine(DirectoryPath, "spirv");
    readonly AssetManifest manifest;
    readonly string manifestHash;
    readonly Action<ProgressState> progress;
    readonly CancellationToken cancel;
    readonly Dictionary<string, (long Size, string Hash)> expected;
    static StringComparison PathComparison => OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

    public sealed record FileRecord(string Path, long Size, string Sha256, long Modified);
    public sealed record Receipt(int Version, string ManifestSha256, string LayoutSha256, List<FileRecord> Files, List<string> Extras,
                                 List<MetadataSegment>? Padding = null);

    public AssetFolder(string root, AssetManifest manifest, Action<ProgressState> progress, CancellationToken cancel,
                       string? directory = null)
    {
        DirectoryPath = Path.GetFullPath(directory ?? Path.Combine(root, "assets"));
        this.manifest = manifest; this.progress = progress; this.cancel = cancel;
        manifestHash = Hash(Path.Combine(root, "assets-required.json"));
        expected = manifest.Files.Where(f => f.RomfsOffset >= 0).ToDictionary(f => f.Path, f => (f.Size, f.Sha256), StringComparer.Ordinal);
        foreach (var m in manifest.Modules) expected.Add(m.Name + ".image", (m.Size, m.ImageSha256));
        foreach (var shader in manifest.Shaders) expected.Add("spirv/" + shader.Hash + ".spv", (-1, ""));
        expected.Add("spirv/results.tsv", (-1, ""));
    }

    void AddExtras(IEnumerable<string> names)
    {
        foreach (string name in names)
        {
            if (!AssetManifest.SafeRelative(name) || name.StartsWith(".catherine", StringComparison.OrdinalIgnoreCase) ||
                name.StartsWith("romfs/blocks/", StringComparison.Ordinal) || expected.ContainsKey(name))
                throw new InvalidDataException("Invalid extra package file: " + name);
            ExtraEntries.Add(name); expected.Add(name, (-1, ""));
        }
    }

    public bool IsReady => File.Exists(Path.Combine(DirectoryPath, ReceiptName));
    public string Resolve(string name)
    {
        string path = AssetManifest.Resolve(DirectoryPath, name);
        // Do not move originals or delete a PAK on the strength of redirected paths.
        for (string? at = path; at != null && at.Length >= DirectoryPath.Length; at = Path.GetDirectoryName(at))
            if ((File.Exists(at) || Directory.Exists(at)) && (File.GetAttributes(at) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Asset folders must not contain symbolic links or junctions: " + at);
        return path;
    }

    string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = new byte[1024 * 1024];
        int n;
        while ((n = stream.Read(buffer)) != 0) { cancel.ThrowIfCancellationRequested(); hash.AppendData(buffer, 0, n); }
        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    void Report(string stage, string detail, double fraction)
    {
        cancel.ThrowIfCancellationRequested();
        progress(new(stage, detail, Math.Clamp(fraction, 0, 1)));
    }

    static void WriteAtomic(string path, byte[] bytes)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            { output.Write(bytes); output.Flush(true); }
            File.Move(temporary, path, true);
        }
        finally { File.Delete(temporary); }
    }

    void ValidateFile(string name, long size, string hash)
    {
        var wanted = expected[name];
        if (wanted.Size >= 0 && size != wanted.Size || wanted.Hash.Length != 0 &&
            !hash.Equals(wanted.Hash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Extracted asset does not match the supported game: " + name);
        if (name.StartsWith("spirv/", StringComparison.Ordinal))
        {
            if (size <= 0 || size > 32 * 1024 * 1024) throw new InvalidDataException("Invalid shader file: " + name);
            if (name.EndsWith(".spv", StringComparison.Ordinal))
            {
                using var file = File.OpenRead(Resolve(name));
                using var reader = new BinaryReader(file);
                if (size % 4 != 0 || size < 20 || reader.ReadUInt32() != 0x07230203)
                    throw new InvalidDataException("Invalid SPIR-V shader: " + name);
            }
        }
    }

    public void Verify(bool full)
    {
        string receiptPath = Resolve(ReceiptName);
        if (new FileInfo(receiptPath).Length > 32 * 1024 * 1024) throw new InvalidDataException("Oversized asset folder record.");
        var receipt = JsonSerializer.Deserialize<Receipt>(File.ReadAllText(receiptPath)) ?? throw new InvalidDataException("Missing asset folder record.");
        Padding.Clear(); Padding.AddRange(receipt.Padding ?? new());
        ValidatePadding();
        if (receipt.Extras == null || receipt.Extras.Count > 100000) throw new InvalidDataException("Invalid extra asset inventory.");
        if (ExtraEntries.Count == 0) AddExtras(receipt.Extras);
        else if (!ExtraEntries.SequenceEqual(receipt.Extras)) throw new InvalidDataException("Extra asset inventory changed.");
        if (receipt.Version != 1 || receipt.ManifestSha256 != manifestHash ||
            receipt.Files.Count != expected.Count || !expected.Keys.ToHashSet(StringComparer.Ordinal).SetEquals(receipt.Files.Select(f => f.Path)) ||
            receipt.Files.Select(f => f.Path).Distinct(StringComparer.Ordinal).Count() != receipt.Files.Count ||
            Hash(Resolve(LayoutName)) != receipt.LayoutSha256)
            throw new InvalidDataException("The extracted asset folder is incomplete or its layout has changed.");
        var updated = new List<FileRecord>();
        foreach (var record in receipt.Files)
        {
            string path = Resolve(record.Path);
            var info = new FileInfo(path);
            if (!info.Exists || info.Length != record.Size) throw new InvalidDataException("Missing or truncated extracted asset: " + record.Path);
            string hash = record.Sha256;
            if (full || info.LastWriteTimeUtc.Ticks != record.Modified)
            {
                Report("Verifying extracted assets", record.Path, updated.Count / (double)expected.Count);
                hash = Hash(path);
                if (hash != record.Sha256) throw new InvalidDataException("Extracted asset checksum mismatch: " + record.Path);
            }
            ValidateFile(record.Path, info.Length, hash);
            updated.Add(record with { Modified = info.LastWriteTimeUtc.Ticks });
        }
        if (!updated.SequenceEqual(receipt.Files))
            WriteAtomic(receiptPath, JsonSerializer.SerializeToUtf8Bytes(receipt with { Files = updated }));
    }

    void ValidatePadding()
    {
        if (Padding.Count > 100000) throw new InvalidDataException("Invalid RomFS padding inventory.");
        long retained = 0;
        var occupied = manifest.Files.Where(f => f.RomfsOffset >= 0).Select(f => (Offset: f.RomfsOffset, Size: f.Size))
            .Concat(manifest.RomfsMetadata.Select(m => (Offset: m.Offset, Size: (long)Convert.FromBase64String(m.Data).Length))).ToList();
        foreach (var segment in Padding)
        {
            long size = Convert.FromBase64String(segment.Data).Length;
            retained += size;
            if (size <= 0 || retained > 8 * 1024 * 1024 || segment.Offset < 0 || segment.Offset > manifest.RomfsSize - size)
                throw new InvalidDataException("Invalid preserved RomFS padding.");
            occupied.Add((segment.Offset, size));
        }
        long end = 0;
        foreach (var range in occupied.OrderBy(r => r.Offset))
        {
            if (range.Offset < end) throw new InvalidDataException("Preserved padding overlaps a RomFS file.");
            end = checked(range.Offset + range.Size);
        }
    }

    public bool MatchesPadding(IEnumerable<MetadataSegment> padding)
        => Padding.Select(m => (m.Offset, m.Data)).SequenceEqual(padding.Select(m => (m.Offset, m.Data)));

    // Shared by native directory reads and package reconstruction. Original RomFS
    // files retain their paths; small filesystem tables supply the virtual offsets.
    void WriteLayout()
    {
        using var output = new MemoryStream();
        using (var writer = new BinaryWriter(output, Encoding.UTF8, true))
        {
            writer.Write("CATHDIR1"u8); writer.Write((ulong)manifest.RomfsSize);
            var raw = manifest.Files.Where(f => f.RomfsOffset >= 0).Select(f => f.Path).ToHashSet(StringComparer.Ordinal);
            var runtime = expected.Keys.Where(n => !raw.Contains(n)).Order(StringComparer.Ordinal).ToList();
            writer.Write((uint)runtime.Count);
            foreach (string name in runtime)
            {
                byte[] text = Encoding.ASCII.GetBytes(name), bytes = File.ReadAllBytes(Resolve(name));
                writer.Write((ushort)text.Length); writer.Write(text);
                writer.Write((ulong)bytes.Length); writer.Write(Crc.Compute(bytes));
            }
            var ranges = manifest.Files.Where(f => f.RomfsOffset >= 0).Select(f => (Offset: f.RomfsOffset, Size: f.Size, Path: f.Path, Data: Array.Empty<byte>()))
                .Concat(manifest.RomfsMetadata.Concat(Padding).Select(m => { byte[] data = Convert.FromBase64String(m.Data); return (Offset: m.Offset, Size: (long)data.Length, Path: "", Data: data); }))
                .OrderBy(r => r.Offset).ToList();
            writer.Write((uint)ranges.Count);
            long end = 0;
            foreach (var range in ranges)
            {
                if (range.Offset < end || range.Size < 0 || range.Offset > manifest.RomfsSize - range.Size)
                    throw new InvalidDataException("Invalid extracted RomFS layout.");
                end = checked(range.Offset + range.Size);
                byte[] path = Encoding.ASCII.GetBytes(range.Path);
                writer.Write((ulong)range.Offset); writer.Write((ulong)range.Size);
                writer.Write((ushort)path.Length); writer.Write(path);
                if (path.Length == 0) writer.Write(range.Data);
            }
        }
        WriteAtomic(Resolve(LayoutName), output.ToArray());
    }

    void Finish()
    {
        var records = new List<FileRecord>();
        foreach (string name in expected.Keys.Order(StringComparer.Ordinal))
        {
            Report("Verifying extracted assets", name, records.Count / (double)expected.Count);
            string path = Resolve(name), hash = Hash(path);
            var info = new FileInfo(path);
            ValidateFile(name, info.Length, hash);
            records.Add(new(name, info.Length, hash, info.LastWriteTimeUtc.Ticks));
        }
        WriteLayout();
        WriteAtomic(Resolve(ReceiptName), JsonSerializer.SerializeToUtf8Bytes(new Receipt(1, manifestHash, Hash(Resolve(LayoutName)), records, ExtraEntries, Padding)));
        Report("Extracted assets ready", "Game files will load directly from assets", 1);
    }

    public void InstallSources(IReadOnlyDictionary<string, string> locations)
    {
        Report("Preparing extracted assets", "Preparing game modules", 0);
        var modules = NsoImage.Prepare(manifest, locations);
        // Check the complete source set before moving any original.
        foreach (var input in manifest.Files)
        {
            cancel.ThrowIfCancellationRequested();
            string source = locations[input.Path], target = Resolve(input.Path);
            if (!input.Accepts(new FileInfo(source).Length, Hash(source))) throw new InvalidDataException("Game file changed during setup: " + input.Path);
            if (File.Exists(target) && !Path.GetFullPath(source).Equals(target, PathComparison) && Hash(target) != Hash(source))
                throw new IOException("A different file already exists in assets: " + input.Path);
        }
        int moved = 0;
        var relocated = new Dictionary<string,string>(OperatingSystem.IsWindows()?StringComparer.OrdinalIgnoreCase:StringComparer.Ordinal);
        foreach (var input in manifest.Files)
        {
            Report("Moving validated game files", input.Path, moved++ / (double)manifest.Files.Count);
            string source = Path.GetFullPath(locations[input.Path]), target = Resolve(input.Path);
            if (source.Equals(target, PathComparison)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            if (!File.Exists(target))
            {
                if (relocated.TryGetValue(source, out string? installed))
                {
                    string temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
                    try { File.Copy(installed, temporary); File.Move(temporary, target); }
                    finally { File.Delete(temporary); }
                }
                else { File.Move(source, target); relocated.Add(source, target); }
            }
            // An identical pre-existing destination is already installed; preserve
            // the duplicate source instead of deleting an unrelated existing copy.
        }
        foreach (var module in modules) WriteAtomic(Resolve(module.Key + ".image"), module.Value);
        Directory.CreateDirectory(ShaderPath);
        Shaders.Generate(manifest, DirectoryPath, ShaderPath, progress, cancel);
        Finish();
    }

    public void ExtractPackage(string pack)
    {
        string parent = Path.GetDirectoryName(DirectoryPath)!;
        if (IsReady)
        {
            // Switching back to folder mode should not need another full copy of
            // an already verified folder. Validate both sides before removing PAK.
            Verify(full: true);
            var before = new FileInfo(pack);
            long size = before.Length, stamp = before.LastWriteTimeUtc.Ticks;
            using (var contents = new PackageContents(pack, manifest))
            {
                if (!ExtraEntries.ToHashSet(StringComparer.Ordinal).SetEquals(contents.ExtraEntries))
                    throw new IOException("assets.pak was kept because its extra files differ from the assets folder.");
                contents.VerifyContent(cancel, progress);
                if (!MatchesPadding(contents.ReadPadding()))
                    throw new IOException("assets.pak was kept because its RomFS padding differs from the assets folder.");
                var raw = manifest.Files.Where(f => f.RomfsOffset >= 0).Select(f => f.Path).ToHashSet(StringComparer.Ordinal);
                foreach (string name in expected.Keys.Where(n => !raw.Contains(n)))
                    if (!contents.ReadEntry(name).AsSpan().SequenceEqual(File.ReadAllBytes(Resolve(name))))
                        throw new IOException("assets.pak was kept because assets contains a different file: " + name);
            }
            DeleteVerifiedPackage(pack, size, stamp);
            return;
        }
        string scratch = Path.Combine(parent, ".assets-extract-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scratch);
        var original = new FileInfo(pack);
        long length = original.Length, modified = original.LastWriteTimeUtc.Ticks;
        try
        {
            var stage = new AssetFolder(parent, manifest, progress, cancel, scratch);
            using (var contents = new PackageContents(pack, manifest))
            {
                AddExtras(contents.ExtraEntries); stage.AddExtras(contents.ExtraEntries);
                foreach (var module in manifest.Modules)
                {
                    byte[] image = contents.ReadEntry(module.Name + ".image");
                    if (image.Length != module.Size || !Convert.ToHexString(SHA256.HashData(image)).Equals(module.ImageSha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Package module mismatch: " + module.Name);
                    WriteAtomic(stage.Resolve(module.Name + ".image"), image);
                }
                var inputs = manifest.Files.Where(f => f.RomfsOffset >= 0).OrderBy(f => f.RomfsOffset).ToList();
                long total = inputs.Sum(f => f.Size), done = 0;
                foreach (var input in inputs)
                {
                    contents.ExtractFile(input, stage.Resolve(input.Path), cancel,
                        copied => Report("Extracting assets.pak", input.Path, (done + copied) / (double)Math.Max(1, total)));
                    done += input.Size;
                }
                foreach (string name in expected.Keys.Where(n => n.StartsWith("spirv/", StringComparison.Ordinal)))
                    WriteAtomic(stage.Resolve(name), contents.ReadEntry(name));
                foreach (string name in ExtraEntries.Where(n => !n.StartsWith("spirv/", StringComparison.Ordinal)))
                    WriteAtomic(stage.Resolve(name), contents.ReadEntry(name));
                contents.VerifyRemaining(cancel);
                Padding.AddRange(contents.ReadPadding()); stage.Padding.AddRange(Padding);
                stage.Finish(); // Re-read and verify the written files before PAK deletion.

                if (!Directory.Exists(DirectoryPath)) Directory.Move(scratch, DirectoryPath);
                else
                {
                    // Check every collision before merging. Never overwrite a user's
                    // different file with the extracted package's version.
                    foreach (string name in expected.Keys)
                    {
                        string target = Resolve(name);
                        if (File.Exists(target) && Hash(target) != stage.Hash(stage.Resolve(name)))
                            throw new IOException("assets.pak was kept because assets contains a different file: " + name);
                    }
                    foreach (string name in expected.Keys)
                    {
                        cancel.ThrowIfCancellationRequested();
                        string target = Resolve(name);
                        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                        if (!File.Exists(target)) File.Move(stage.Resolve(name), target);
                    }
                    Finish();
                }
            }
            DeleteVerifiedPackage(pack, length, modified);
        }
        finally
        {
            // This unique directory is created and owned by this operation only.
            if (Directory.Exists(scratch)) Directory.Delete(scratch, true);
        }
    }

    void DeleteVerifiedPackage(string pack, long length, long modified)
    {
        cancel.ThrowIfCancellationRequested();
        var current = new FileInfo(pack);
        if (current.Length != length || current.LastWriteTimeUtc.Ticks != modified)
            throw new IOException("assets.pak changed during extraction and was not deleted.");
        File.Delete(pack);
        File.Delete(Path.Combine(Path.GetDirectoryName(DirectoryPath)!, "assets-verification.json"));
        Report("Extracted assets ready", "Verified assets folder; removed assets.pak", 1);
    }

    public Dictionary<string, byte[]> PreparedModules()
        => manifest.Modules.ToDictionary(m => m.Name, m => File.ReadAllBytes(Resolve(m.Name + ".image")), StringComparer.Ordinal);
}
