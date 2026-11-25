"""Stage the supplied PC UI artwork and build the runtime's lossless icon bundle."""
from pathlib import Path
import hashlib,io,json,shutil,struct
from PIL import Image,ImageOps
ROOT=Path(__file__).resolve().parents[2]
def main():
 target=ROOT/'Source/CathAssets';target.mkdir(exist_ok=True);icons=target/'Prompts'
 shutil.copytree(ROOT/'keysource',icons,dirs_exist_ok=True)
 records=[];extra=Path('C:/Users/scott/Desktop/ps4-and-keyboard-mouse-tutorial-images');actions=['dpad','left-trigger','right-stick','right-trigger']
 families={'PS5':[Path('C:/Users/scott/Desktop/Old')/n for n in ['1 (2).png','2 (2).png','3.png','4.png']]}
 for family,prefix in [('Xbox','xbox-controller'),('Switch','switch'),('PS4','ps4'),('KeyboardMouse','kb')]:families[family]=[extra/f'{prefix}-{a}.png'for a in actions]
 families['KeyboardMouse'][3]=ROOT/'Source/artwork/keyboard-autoplay-e.png'
 for family,sources in families.items():
  tutorial=target/'Tutorials'/family;tutorial.mkdir(parents=True,exist_ok=True);bc3=[]
  for i,src in enumerate(sources,1):
   dst=tutorial/f'{i}.png';shutil.copyfile(src,dst)
   canvas=Image.new('RGBA',(1024,1024));image=ImageOps.contain(Image.open(dst).convert('RGBA'),(480,337),Image.Resampling.LANCZOS);canvas.alpha_composite(image,(130+(480-image.width)//2,168+(337-image.height)//2))
   output=io.BytesIO();canvas.save(output,format='DDS',pixel_format='DXT5');data=output.getvalue();assert data[84:88]==b'DXT5'and len(data)==1048576+128;bc3.append(data[128:])
   record=dict(path=dst.relative_to(target).as_posix(),source=src.name,sha256=hashlib.sha256(dst.read_bytes()).hexdigest())
   if src.is_relative_to(ROOT):record['source_path']=src.relative_to(ROOT).as_posix()
   records.append(record)
  (tutorial/'tutorial.bc3').write_bytes(b''.join(bc3))
 for folder in [target,ROOT/'Build/CathAssets']:(folder/'dualsense-tutorial.bc3').unlink(missing_ok=True)
 images=[]
 for p in sorted(icons.rglob('*.png')):
  if 'sprite'in p.name.lower():continue
  image=Image.open(p).convert('RGBA');bounds=image.getbbox()
  if bounds:image=image.crop(bounds)
  name=p.relative_to(icons).as_posix().encode();data=image.tobytes();images.append(struct.pack('<H',len(name))+name+struct.pack('<HHI',*image.size,len(data))+data)
 (target/'prompts.rgba').write_bytes(b'CATRIMG1'+struct.pack('<I',len(images))+b''.join(images))
 (target/'artwork-manifest.json').write_text(json.dumps(dict(icons=len(images),tutorials=records),indent=2)+'\n')
 shutil.copytree(target,ROOT/'Build/CathAssets',dirs_exist_ok=True)
 print('Prepared CathAssets:',len(images),'individual icons and twenty tutorial PNGs across five input families; runtime bundle', (target/'prompts.rgba').stat().st_size,'bytes')
if __name__=='__main__':main()
