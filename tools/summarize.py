#!/usr/bin/env python3
"""Export raw measurements and query-first means; failures never become zero time."""
import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path

def write_csv(path,rows):
    columns=list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=columns);w.writeheader();w.writerows(rows)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
    rows=[];depth=[];stats=[]
    for path in sorted(Path(a.input).glob('run_*/run.json')):
        r=json.loads(path.read_text());c=r['configuration'];mp=path.parent/'metrics.json';m=json.loads(mp.read_text()) if mp.exists() else {}
        key={k:r[k] for k in ('dataset','group','query','mode','repeat')};key.update(gpu_id=c.get('gpu_id',0),batch_capacity=c.get('batch_capacity',65536),candidate_tile_size=c.get('candidate_tile_size',65536),gpu_memory_mb=c.get('gpu_memory_mb',0),backend=r.get('backend',m.get('backend','historical_cpu')),timing_method=r.get('timing_method',m.get('timing_method','cpu_thread_wall_sum')))
        row={**key,'status':r['status'],'exit_code':r['exit_code'],'embedding_count':m.get('embedding_count'),'partial_embedding_count':m.get('partial_embedding_count')}
        for field in ('matching_time_s','candidate_generation_time_s','embedding_validation_time_s','host_to_device_time_s','cuda_initialize_time_s','device_layout_time_s','plan_time_s','online_query_time_s','index_build_time_s','preprocessing_time_s','graph_storage_bytes','signature_storage_bytes','inverted_index_bytes','peak_rss_bytes','rss_after_index_bytes','root_candidates','candidates_total','vertex_count_pass_total','valid_partial_total'):
            row[field]=m.get(field)
        # 3600 is the paper's censoring policy, only for matching online timeouts at that configured limit.
        row['paper_capped_time_s']=m.get('matching_time_s') if r['status']=='ok' else 3600.0 if r['status']=='timeout' and r['timeout_seconds']==3600 else None
        rows.append(row)
        for d in m.get('per_depth',[]):depth.append({**key,**d,'status':r['status']})
        for kind in ('data','query'):
            if kind in m:stats.append({**key,'kind':kind,**m[kind]})
    grouping=('dataset','group','query','mode','gpu_id','batch_capacity','candidate_tile_size','gpu_memory_mb','backend','timing_method')
    buckets=defaultdict(list)
    for row in rows:buckets[tuple(row[k] for k in grouping)].append(row)
    per_query=[]
    for key,items in buckets.items():
        ok=[r for r in items if r['status']=='ok'];counts={r['embedding_count'] for r in ok}
        means={**dict(zip(grouping,key)),'runs':len(items),'completed_runs':len(ok),'all_runs_completed':len(ok)==len(items),'count_consistent':len(counts)<=1,'embedding_count':next(iter(counts)) if len(counts)==1 else None}
        for field in ('matching_time_s','candidate_generation_time_s','embedding_validation_time_s','online_query_time_s','paper_capped_time_s'):
            values=[r[field] for r in items if r[field] is not None and (field=='paper_capped_time_s' or r['status']=='ok')]
            means['mean_'+field]=statistics.mean(values) if len(values)==len(items) else None
        per_query.append(means)
    groupkeys=tuple(x for x in grouping if x!='query');groups=defaultdict(list)
    for row in per_query:groups[tuple(row[k] for k in groupkeys)].append(row)
    summary=[]
    for key,queries in groups.items():
        g={**dict(zip(groupkeys,key)),'queries':len(queries),'completed_queries':sum(q['all_runs_completed'] for q in queries),'completed_runs':sum(q['completed_runs'] for q in queries),'runs':sum(q['runs'] for q in queries)}
        g['query_completion_ratio']=g['completed_queries']/g['queries'];g['run_completion_ratio']=g['completed_runs']/g['runs']
        for field in ('matching_time_s','candidate_generation_time_s','embedding_validation_time_s','online_query_time_s','paper_capped_time_s'):
            values=[q['mean_'+field] for q in queries if q['mean_'+field] is not None]
            g['mean_'+field]=statistics.mean(values) if len(values)==len(queries) else None
        summary.append(g)
    for name,records in [('runs',rows),('per_query',per_query),('summary',summary),('filtering',depth),('graph_statistics',stats)]:write_csv(out/(name+'.csv'),records)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'README.md').write_text('# 实验汇总\n\n先对每个查询的重复求均值，再对同组查询等权平均。只有完整重复集合具有主时间均值；未完成不填 0。完成查询要求该查询所有重复成功。paper_capped_time_s 仅把配置为 3600 秒的程序 timeout 记为 3600，外部强杀或输入错误仍为空。GPU编号、批量容量、候选窗口、显存预算、后端和计时方式均分组。runs.csv 保留原始状态和时间及算法4/5累计秒数，per_query.csv及summary.csv/JSON保留mean_candidate_generation_time_s和mean_embedding_validation_time_s；算法时间不采用3600秒截断，失败或缺失不填0。GPU算法时间是单stream CUDA Events阶段区间之和，不累加GPU线程时间。CPU历史时间另按后端和计时方式分组。filtering.csv 保留每层 C/F/V 和算法时间，graph_statistics.csv 保留数据统计。CSV 空字段为未获得/不适用。\n')
    print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
