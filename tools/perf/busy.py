#!/usr/bin/env python3
"""busy.py SAMPLE.txt ...: per thread of a `sample` file, the share of samples not in a kernel wait, and its hottest leaves."""
import re, sys, collections
WAIT = ('__psynch_cvwait','__ulock_wait','mach_msg2_trap','__workq_kernreturn','__semwait_signal','semaphore_wait_trap',
        'kevent','__select','__psynch_mutexwait','__sigwait','semaphore_timedwait_trap','__psynch_rw_','mach_msg_trap','start_wqthread','__bsdthread_ctl')
def one(path):
    L = open(path, errors='replace').read().split('\n')
    a = next(i for i,l in enumerate(L) if l.startswith('Call graph:'))
    b = next(i for i,l in enumerate(L) if l.startswith('Total number in stack'))
    threads = []; cur = None
    for l in L[a+1:b]:
        m = re.match(r'^\s+(\d+) (Thread_\S+.*)$', l)
        if m: cur = {'name': m.group(2)[:44], 'total': int(m.group(1)), 'idle': 0, 'rows': []}; threads.append(cur); continue
        m = re.match(r'^([\s+!:|]*)(\d+) (.*?)  \(in (.*?)\)', l)
        if m and cur is not None: cur['rows'].append((len(m.group(1)), int(m.group(2)), m.group(3).strip(), m.group(4)))
    out = []
    for t in threads:
        rows = t['rows']; leaf = collections.Counter()
        for i,(d,n,name,img) in enumerate(rows):
            kids = 0; j = i+1
            while j < len(rows) and rows[j][0] > d:
                if rows[j][0] == rows[i+1][0]: kids += rows[j][1]
                j += 1
            if n - kids > 0: leaf[(name,img)] += n - kids
        idle = sum(v for (n,_),v in leaf.items() if n.startswith(WAIT))
        busy = sum(leaf.values()) - idle
        out.append((busy, t['total'], t['name'], [(v,n) for (n,_),v in leaf.most_common(40) if not n.startswith(WAIT)][:5]))
    return out
for p in sys.argv[1:]:
    r = one(p); tot = sum(b for b,_,_,_ in r); per = max(t for _,t,_,_ in r)
    print('%s: %.2f cores busy in all' % (p.split('/')[-1], tot/per))
    for busy,total,name,top in sorted(r, reverse=True)[:8]:
        if busy < 0.03*total: continue
        print('  %5.1f%%  %-30s %s' % (100*busy/total, name[:30], '; '.join('%s %d' % (n[:26],v) for v,n in top)))
