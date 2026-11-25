using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
namespace Catherine.AssetSetup;

sealed class NsoImage {
 public byte[] Image;
 public record Symbol(string Name,byte Info,ushort Section,ulong Value);
 public record Relocation(ulong Offset,uint Kind,int Symbol,long Addend);
 public List<Symbol> Symbols=new(); public List<Relocation> Relocations=new();
 uint U32(int o)=>BinaryPrimitives.ReadUInt32LittleEndian(Image.AsSpan(o,4));
 ulong U64(int o)=>BinaryPrimitives.ReadUInt64LittleEndian(Image.AsSpan(o,8));
 string Str(int o){int end=Array.IndexOf(Image,(byte)0,o);if(end<0)throw new InvalidDataException("Unterminated NSO symbol.");return Encoding.UTF8.GetString(Image,o,end-o);}
 public NsoImage(string path,int expectedSize){
  var raw=File.ReadAllBytes(path);
  if(raw.Length<256||!raw.AsSpan(0,4).SequenceEqual("NSO0"u8))throw new InvalidDataException("Not an extracted NSO: "+path);
  uint flags=BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(12,4));Image=new byte[expectedSize];
  for(int i=0;i<3;i++){
   int at=0x10+i*16,off=(int)BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(at,4)),mem=(int)BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(at+4,4)),length=(int)BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(at+8,4));
   int compressed=(int)BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(0x60+4*i,4));
   if(off<0||mem<0||length<0||off>raw.Length-compressed||mem>Image.Length-length)throw new InvalidDataException("NSO segment is outside its image.");
   if((flags&(1u<<i))!=0)Lz4(raw.AsSpan(off,compressed),Image.AsSpan(mem,length));else raw.AsSpan(off,length).CopyTo(Image.AsSpan(mem,length));
  }
  ModuleCompatibility.Normalize(raw,Image);
  int mod=(int)U32(4);if(!Image.AsSpan(mod,4).SequenceEqual("MOD0"u8))throw new InvalidDataException("NSO module information is missing.");
  int dynamic=checked(mod+(int)U32(mod+4));var dyn=new Dictionary<ulong,ulong>();
  for(int at=dynamic;;at+=16){ulong tag=U64(at),value=U64(at+8);if(tag==0)break;dyn.TryAdd(tag,value);}
  int count=0;if(dyn.TryGetValue(4,out var hash))count=(int)U32((int)hash+4);
  else if(dyn.TryGetValue(0x6ffffef5,out hash)){
   int h=(int)hash,buckets=(int)U32(h),first=(int)U32(h+4),bloom=(int)U32(h+8),start=h+16+8*bloom,last=0;
   for(int i=0;i<buckets;i++)last=Math.Max(last,(int)U32(start+4*i));
   if(last<first)count=first;else{int chains=start+4*buckets;while((U32(chains+4*(last-first))&1)==0)last++;count=last+1;}
  }
  if(count>0&&dyn.TryGetValue(6,out var symbols))for(int i=0;i<count;i++){
   int at=checked((int)symbols+24*i);Symbols.Add(new(Str(checked((int)dyn[5]+(int)U32(at))),Image[at+4],BinaryPrimitives.ReadUInt16LittleEndian(Image.AsSpan(at+6,2)),U64(at+8)));
  }
  foreach(var pair in new[]{(7UL,8UL),(23UL,2UL)})if(dyn.TryGetValue(pair.Item1,out var reloc)){
   for(ulong i=0;i<dyn[pair.Item2]/24;i++){int at=checked((int)(reloc+24*i));ulong info=U64(at+8);Relocations.Add(new(U64(at),(uint)info,(int)(info>>32),unchecked((long)U64(at+16))));}
  }
  if(dyn.TryGetValue(0x24,out var relr)){
   ulong where=0;
   for(ulong i=0;i<dyn[0x23]/8;i++){
    ulong v=U64(checked((int)(relr+8*i)));
    if((v&1)==0){Relocations.Add(new(v,1027,0,unchecked((long)U64((int)v))));where=v+8;}
    else{for(int k=0;k<63;k++)if((v&(1UL<<(k+1)))!=0){ulong a=where+(ulong)k*8;Relocations.Add(new(a,1027,0,unchecked((long)U64((int)a))));}where+=63*8;}
   }
  }
 }
 static void Lz4(ReadOnlySpan<byte> src,Span<byte> dst){
  int s=0,d=0;while(s<src.Length){int token=src[s++],n=token>>4;if(n==15){int b;do{b=src[s++];n+=b;}while(b==255);}src.Slice(s,n).CopyTo(dst.Slice(d,n));s+=n;d+=n;if(s==src.Length)break;int offset=src[s]|src[s+1]<<8;s+=2;if(offset==0||offset>d)throw new InvalidDataException("Invalid LZ4 offset.");n=(token&15)+4;if((token&15)==15){int b;do{b=src[s++];n+=b;}while(b==255);}for(int i=0;i<n;i++)dst[d+i]=dst[d+i-offset];d+=n;}if(d!=dst.Length)throw new InvalidDataException("NSO decompression length mismatch.");
 }
 public static Dictionary<string,byte[]> Prepare(AssetManifest manifest,IReadOnlyDictionary<string,string> locations){
  var modules=manifest.Modules.Select(spec=>(spec,nso:new NsoImage(locations["exefs/"+spec.Name],spec.Size))).ToList();var exports=new Dictionary<string,ulong>(StringComparer.Ordinal);var missing=new Dictionary<string,ulong>(StringComparer.Ordinal);
  foreach(var(spec,nso)in modules)foreach(var sym in nso.Symbols)if((sym.Info>>4)is 1 or 2&&sym.Section!=0)exports.TryAdd(sym.Name,sym.Section==0xfff1?sym.Value:spec.Base+sym.Value);
  var result=new Dictionary<string,byte[]>();
  foreach(var(spec,nso)in modules){
   foreach(var r in nso.Relocations){
    ulong value;
    if(r.Kind==1027)value=unchecked(spec.Base+(ulong)r.Addend);
    else if(r.Kind is 257 or 1025 or 1026){var sym=nso.Symbols[r.Symbol];if(sym.Section!=0)value=sym.Section==0xfff1?sym.Value:spec.Base+sym.Value;else if(exports.TryGetValue(sym.Name,out var found))value=found;else if(sym.Info>>4==2)value=0;else{if(!missing.TryGetValue(sym.Name,out value)){value=0x10000+(ulong)missing.Count*16;missing.Add(sym.Name,value);}}value=unchecked(value+(ulong)r.Addend);}
    else throw new InvalidDataException("Unsupported NSO relocation: "+r.Kind);
    BinaryPrimitives.WriteUInt64LittleEndian(nso.Image.AsSpan(checked((int)r.Offset),8),value);
   }
   if(Convert.ToHexString(SHA256.HashData(nso.Image)).ToLowerInvariant()!=spec.ImageSha256)throw new InvalidDataException("Prepared module does not match the supported version: "+spec.Name);
   result[spec.Name]=nso.Image;
  }
  return result;
 }
}
