using System.IO.Compression;
using System.Security.Cryptography;

namespace Catherine.AssetSetup;

/* Convert the verified alternate NSO before reading its dynamic symbols or
   relocations: its exported addresses also affect the SDK modules. The delta
   contains XOR corrections, not a replacement executable. Both paths must
   still pass every canonical relocated-module hash in NsoImage.Prepare. */
static class ModuleCompatibility {
 const int MainSize=63758336;
 const string FileHash="0d52cea4fd3cf1d7e88bf143a89e5579bf699f7db0ece365a54380b50d6e2879";
 const string InputImageHash="ff27ac30148d465d2fa97633ced412ac06699da66ba541ea09ade5f4bde00b07";
 const string OutputImageHash="a556418d928110d4de01d1b849032d7cbbbd6169e64e2f9cde4ec1adc250830f";
 static string Hash(ReadOnlySpan<byte> data)=>Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
 public static bool Normalize(ReadOnlySpan<byte> nso,byte[] image){
  if(nso.Length!=7318119||Hash(nso)!=FileHash)return false;
  if(image.Length!=MainSize||Hash(image)!=InputImageHash)throw new InvalidDataException("Alternate main has an unexpected module image.");
  using var resource=typeof(ModuleCompatibility).Assembly.GetManifestResourceStream("Catherine.AssetSetup.Compatibility.Main93a79c77")??throw new InvalidDataException("Alternate main compatibility data is missing.");
  using var delta=new ZLibStream(resource,CompressionMode.Decompress);
  byte[] block=new byte[65536];int position=0;
  while(position<image.Length){
   int count=delta.Read(block,0,Math.Min(block.Length,image.Length-position));
   if(count==0)throw new InvalidDataException("Alternate main compatibility data is truncated.");
   for(int i=0;i<count;i++)image[position+i]^=block[i];
   position+=count;
  }
  if(delta.ReadByte()!=-1||Hash(image)!=OutputImageHash)throw new InvalidDataException("Alternate main compatibility validation failed.");
  return true;
 }
}
