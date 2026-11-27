using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace Catherine.AssetSetup;

// A bounded, validating reader used before a package can be replaced by a folder.
sealed class PackageContents : IDisposable
{
    const int BlockSize = 262144;
    readonly FileStream file;
    readonly ZipArchive zip;
    readonly AssetManifest manifest;
    readonly Dictionary<string, ZipArchiveEntry> entries;
    readonly HashSet<int> checkedBlocks = new();
    public IReadOnlyList<string> ExtraEntries { get; }
    int cachedIndex = -1;
    byte[] cached = Array.Empty<byte>();

    public PackageContents(string path, AssetManifest manifest)
    {
        this.manifest = manifest;
        file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        try
        {
            var methods = PackageUpdate.Directory(file,backupShaders:false).Methods;
            file.Position = 0;
            zip = new ZipArchive(file, ZipArchiveMode.Read, true, Encoding.UTF8);
            entries = new(StringComparer.OrdinalIgnoreCase);
            foreach (var entry in zip.Entries)
                if (!AssetManifest.SafeRelative(entry.FullName) || entry.Length > 512L * 1024 * 1024 ||
                    !methods.TryGetValue(entry.FullName, out var method) || method is not (0 or 8) ||
                    !entries.TryAdd(entry.FullName, entry))
                    throw new InvalidDataException("Invalid or duplicate asset package entry: " + entry.FullName);

            var expected = new HashSet<string>(StringComparer.Ordinal);
            foreach (var module in manifest.Modules) expected.Add(module.Name + ".image");
            int count = checked((int)((manifest.RomfsSize + BlockSize - 1) / BlockSize));
            for (int i = 0; i < count; i++) expected.Add($"romfs/blocks/{i:D6}.bin");
            foreach (var shader in manifest.Shaders) expected.Add("spirv/" + shader.Hash + ".spv");
            expected.Add("spirv/results.tsv");
            expected.Add("catherine-pak.manifest");
            if (!expected.IsSubsetOf(entries.Keys))
                throw new InvalidDataException("The asset package does not contain the expected game files and shaders.");
            ExtraEntries = entries.Keys.Where(n => !expected.Contains(n)).Order(StringComparer.Ordinal).ToArray();
            if (ExtraEntries.Any(n => n.StartsWith("romfs/blocks/", StringComparison.Ordinal) ||
                n.StartsWith(".catherine", StringComparison.OrdinalIgnoreCase) || manifest.Files.Any(f => f.Path == n)))
                throw new InvalidDataException("Unexpected or conflicting asset package entries.");

            using var directory = new MemoryStream();
            using (var writer = new BinaryWriter(directory, Encoding.UTF8, true))
                foreach (var entry in entries.Values.OrderBy(e => e.FullName, StringComparer.Ordinal))
                {
                    if (entry.FullName == "catherine-pak.manifest") continue;
                    byte[] name = Encoding.ASCII.GetBytes(entry.FullName);
                    writer.Write((ushort)name.Length); writer.Write(name);
                    writer.Write((ulong)entry.Length); writer.Write(entry.Crc32); writer.Write(methods[entry.FullName]);
                }
            string wanted = $"CATHERINE_PAK 1\nROMFS_SIZE {manifest.RomfsSize}\nBLOCK_SIZE {BlockSize}\nENTRY_COUNT {entries.Count - 1}\nDIRECTORY_CRC32 {Crc.Compute(directory.ToArray()):x8}\n";
            if (entries["catherine-pak.manifest"].Length > 512 ||
                Encoding.ASCII.GetString(ReadEntry("catherine-pak.manifest")) != wanted)
                throw new InvalidDataException("Asset package manifest/directory mismatch.");
        }
        catch { file.Dispose(); throw; }
    }

    public byte[] ReadEntry(string name)
    {
        if (!entries.TryGetValue(name, out var entry)) throw new InvalidDataException("Missing package entry: " + name);
        byte[] result = new byte[checked((int)entry.Length)];
        using var stream = entry.Open();
        stream.ReadExactly(result);
        if (stream.ReadByte() != -1 || Crc.Compute(result) != entry.Crc32)
            throw new InvalidDataException("Asset package CRC mismatch: " + name);
        return result;
    }

    byte[] Block(int index)
    {
        if (cachedIndex != index)
        {
            cached = ReadEntry($"romfs/blocks/{index:D6}.bin");
            if (cached.LongLength != Math.Min(BlockSize, manifest.RomfsSize - (long)index * BlockSize))
                throw new InvalidDataException("Invalid RomFS block length.");
            cachedIndex = index;
            checkedBlocks.Add(index);
        }
        return cached;
    }

    public void ReadRomfs(long offset, Span<byte> destination)
    {
        if (offset < 0 || offset > manifest.RomfsSize || destination.Length > manifest.RomfsSize - offset)
            throw new InvalidDataException("RomFS range is outside the package.");
        while (!destination.IsEmpty)
        {
            byte[] block = Block(checked((int)(offset / BlockSize)));
            int within = (int)(offset % BlockSize), take = Math.Min(destination.Length, block.Length - within);
            block.AsSpan(within, take).CopyTo(destination);
            destination = destination[take..]; offset += take;
        }
    }

    public List<MetadataSegment> ReadPadding()
    {
        var ranges = manifest.Files.Where(f => f.RomfsOffset >= 0).Select(f => (Offset: f.RomfsOffset, Size: f.Size))
            .Concat(manifest.RomfsMetadata.Select(m => (Offset: m.Offset, Size: (long)Convert.FromBase64String(m.Data).Length)))
            .OrderBy(r => r.Offset).ToList();
        var result = new List<MetadataSegment>();
        long position = 0, retained = 0;
        void Gap(long end)
        {
            while (position < end)
            {
                byte[] bytes = new byte[(int)Math.Min(end - position, 1024 * 1024)];
                ReadRomfs(position, bytes);
                if (bytes.Any(b => b != 0))
                {
                    retained += bytes.Length;
                    if (retained > 8 * 1024 * 1024) throw new InvalidDataException("Unexpectedly large nonzero RomFS padding.");
                    result.Add(new MetadataSegment { Offset = position, Data = Convert.ToBase64String(bytes) });
                }
                position += bytes.Length;
            }
        }
        foreach (var range in ranges)
        {
            if (range.Offset < position || range.Size < 0 || range.Offset > manifest.RomfsSize - range.Size)
                throw new InvalidDataException("Invalid RomFS layout while preserving padding.");
            Gap(range.Offset); position = range.Offset + range.Size;
        }
        Gap(manifest.RomfsSize);
        return result;
    }

    public void ExtractFile(InputFile input, string target, CancellationToken cancel, Action<long> progress)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        CopyFile(input, output, cancel, progress);
        output.Flush(true);
    }

    void CopyFile(InputFile input, Stream output, CancellationToken cancel, Action<long> progress)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = new byte[BlockSize];
        long copied = 0;
        while (copied < input.Size)
        {
            cancel.ThrowIfCancellationRequested();
            int take = (int)Math.Min(buffer.Length, input.Size - copied);
            ReadRomfs(input.RomfsOffset + copied, buffer.AsSpan(0, take));
            output.Write(buffer, 0, take); hash.AppendData(buffer, 0, take);
            copied += take; progress(copied);
        }
        if (!input.Accepts(copied, Convert.ToHexString(hash.GetHashAndReset())))
            throw new InvalidDataException("Extracted game file does not match the supported version: " + input.Path);
    }

    public void VerifyContent(CancellationToken cancel, Action<ProgressState> progress)
    {
        foreach (var module in manifest.Modules)
        {
            byte[] bytes = ReadEntry(module.Name + ".image");
            if (bytes.Length != module.Size || !Convert.ToHexString(SHA256.HashData(bytes)).Equals(module.ImageSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Package module mismatch: " + module.Name);
        }
        var inputs = manifest.Files.Where(f => f.RomfsOffset >= 0).OrderBy(f => f.RomfsOffset).ToList();
        long total = inputs.Sum(f => f.Size), done = 0;
        foreach (var input in inputs)
        {
            CopyFile(input, Stream.Null, cancel, copied => progress(new("Verifying rebuilt assets.pak", input.Path, (done + copied) / (double)Math.Max(1, total))));
            done += input.Size;
        }
        foreach (string name in entries.Keys.Where(n => n.StartsWith("spirv/", StringComparison.Ordinal)).Concat(ExtraEntries).Distinct())
        {
            cancel.ThrowIfCancellationRequested();
            byte[] bytes = ReadEntry(name);
            if (name.EndsWith(".spv", StringComparison.Ordinal) && (bytes.Length < 20 || bytes.Length % 4 != 0 || BitConverter.ToUInt32(bytes) != 0x07230203))
                throw new InvalidDataException("Invalid packaged SPIR-V: " + name);
        }
        VerifyRemaining(cancel);
    }

    public void VerifyRemaining(CancellationToken cancel)
    {
        foreach (var segment in manifest.RomfsMetadata)
        {
            byte[] wanted = Convert.FromBase64String(segment.Data), actual = new byte[wanted.Length];
            ReadRomfs(segment.Offset, actual);
            if (!actual.AsSpan().SequenceEqual(wanted)) throw new InvalidDataException("RomFS metadata mismatch.");
        }
        int count = checked((int)((manifest.RomfsSize + BlockSize - 1) / BlockSize));
        for (int i = 0; i < count; i++) { cancel.ThrowIfCancellationRequested(); if (!checkedBlocks.Contains(i)) Block(i); }
    }

    public void Dispose() { zip.Dispose(); file.Dispose(); }
}
