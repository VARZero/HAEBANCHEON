"""Parse complete native primitive run; times are direct board monotonic ns."""
import argparse,json,statistics,math,hashlib
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
text=a.log.read_text(errors='replace');assert 'PRIMITIVE_BEGIN' in text and 'PRIMITIVE_DONE' in text and 'PIXEL_MISMATCH' not in text and 'BOSIO:' not in text
names=['publish_only','tile_dma_fill','tile_dma_copy','tile_upload','masked_solid','masked_copy_with_upload']
groups={}
for line in text.splitlines():
 if not line.startswith('P '):continue
 v=list(map(int,line.split()[1:]));assert len(v)==12
 mode,layout,side,sample,*times=v
 key=(mode,layout,side);r=groups.setdefault(key,dict(mode=names[mode],layout='scattered' if layout else 'contiguous_triangle',side_cells=side,selected_cells=0 if mode==0 else side*side,samples=[]))
 r['samples'].append(dict(index=sample,write_ms=times[0]/1e6,blend_only_ms=times[1]/1e6,publish_ms=times[2]/1e6,total_ms=times[3]/1e6,validate_ms=times[4]/1000,metadata_upload_ms=times[5]/1000,wait_ms=times[6]/1000,metadata_uploaded_words=times[7]))
assert len(groups)==28,len(groups)
for r in groups.values():
 assert [s['index'] for s in r['samples']]==list(range(10))
 r['metrics']={}
 for k in ['write_ms','blend_only_ms','publish_ms','total_ms','validate_ms','metadata_upload_ms','wait_ms']:
  v=sorted(s[k] for s in r['samples']);r['metrics'][k]=dict(median=statistics.median(v),p95=v[math.ceil(.95*len(v))-1],maximum=max(v))
 print(r['mode'],r['layout'],r['selected_cells'],*[round(r['metrics'][k]['median'],3) for k in ['write_ms','publish_ms','total_ms']])
copy_only=all(s['metadata_uploaded_words']==4476 for r in groups.values() for s in r['samples'])
a.output.write_text(json.dumps(dict(source=a.log.name,publication='BO_PUBLISH_COPY' if copy_only else 'BO_PUBLISH',scope='WM/app/input/projection excluded. Prebuilt masks and source buffers. Direct synchronous ioctl wall time on board. Separate inactive-bank payload write, publication completion, and their sum. Final sample of every case verified by scratch readback outside timed intervals. No optical latency.',warmups_per_case=2,samples_per_case=10,groups=list(groups.values())),indent=2)+'\n',encoding='utf-8')
