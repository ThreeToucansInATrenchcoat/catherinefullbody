"""Validate complete roster coverage and the rendered native skin palettes."""
from pathlib import Path
import argparse,json,math,struct
p=argparse.ArgumentParser();p.add_argument('run');args=p.parse_args();run=Path(args.run)
results=json.loads((run/'pair-results.json').read_text());summary=[]
for case in results:
 entry={'index':case['index'],'name':case['name'],'rates':[]}
 for sample in case['samples']:
  data=json.loads(Path(sample['path']).read_text());seen=set();checked=0;bad=0;players=set()
  for row in data['rows']:
   players.add(row['player']);assert len(row['position'])==3 and all(math.isfinite(x)for x in row['position'])
   for palette in row['palettes']:
    value=palette['hex']
    if value in seen:continue
    seen.add(value);raw=bytes.fromhex(value);assert len(raw)==row['count']*64
    for offset in range(0,len(raw),64):
     checked+=1;bad+=not all(math.isfinite(x)for x in struct.unpack_from('<12f',raw,offset))
  assert players=={1,2} and checked>0 and bad==0,(case['name'],sample['fps'],players,checked,bad)
  assert (sample['counter_delta']==[0,0,0,0])if sample['fps']==30 else(sample['counter_delta'][0]>0 and sample['counter_delta'][1]>0)
  entry['rates'].append({'fps':sample['fps'],'players':sorted(players),'unique_palettes':len(seen),'checked_bone_matrices':checked,'nonfinite':bad,'counter_delta':sample['counter_delta']})
 assert {x['fps']for x in entry['rates']}=={30,60};summary.append(entry);print(case['name'],'passed',flush=True)
report={'complete':len(summary)==13 and {x['index']for x in summary}==set(range(13)),'characters':summary}
(run/'validated-results.json').write_text(json.dumps(report,indent=2));print(json.dumps({'complete':report['complete'],'characters':len(summary)}))
