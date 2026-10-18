#!/usr/bin/env python3
"""The network calls of one recorded process, in order with runs compressed, or counted.

Reads a filtered relay log on stdin and prints to stdout; it opens no file.  It was run on the
machine that holds the recording, never here (docs/trace/net-calls.md has the exact command):

    grep -a ':04e8:[0-9a-f]*:\\(Call \\(ws2_32\\|wsock32\\|iphlpapi\\)\\.\\|Ret  \\(ws2_32\\|wsock32\\|iphlpapi\\)\\.\\|trace:loaddll\\)' <log> \\
        | python3 -c "<this file>" counts|phases|list [first last [from-second]]

The pid (04e8), the time of the process's first line (T0) and the marker times are those of
the capture a3-mission1 and are written into the script.  What it prints is safe to keep:
a name given to getaddrinfo is printed as <host N> or <address N> unless it is "localhost"
or a loopback address in text; no buffer content is in a relay line.  A call whose caller is
one of EA's modules is counted with its arguments left out.  Sockets are numbered S1, S2, ...
in the order `socket` returned them.  An int argument is printed by relay with the upper half
of its register, so a length can read as a very large number: its low 32 bits are the value.
"""
import sys,re,bisect,collections
T0=1467891.323
MARK=[(1467897.469,'respawn.bik opened'),(1467903.107,'ea.bik opened'),(1467911.797,'menu_act01.bik opened: MAIN MENU'),(1467920.166,'sp_training_loadscreen.rpak opened: MISSION LOAD')]
MODE=sys.argv[1] if len(sys.argv)>1 else 'list'
LO=int(sys.argv[2]) if len(sys.argv)>2 else 0
HI=int(sys.argv[3]) if len(sys.argv)>3 else 400
TMIN=float(sys.argv[4]) if len(sys.argv)>4 else 0.0
call=re.compile(rb'^(\d+\.\d+):04e8:([0-9a-f]+):Call (\w+)\.(\w+)\((.*)\) ret=([0-9a-f]+)\s*$')
retr=re.compile(rb'^(\d+\.\d+):04e8:([0-9a-f]+):Ret  (\w+)\.(\w+)\(\) retval=([0-9a-f]+) ret=([0-9a-f]+)\s*$')
load=re.compile(rb'Loaded L"(?:[^"]*\\)?([^\\"]*)" at ([0-9a-fA-F]+)')
tn=re.compile(rb'^[\d.]+:04e8:([0-9a-f]+):\w+:threadname:\S+ Thread (?:ID ([0-9a-f]+) )?renamed to L?"([^"]*)"')
EA=('originsdk','igo64','activation64')
raw=[];bases=[];recs=[];open_=collections.defaultdict(list);tnames={}
for line in sys.stdin.buffer:
    m=call.match(line)
    if m:
        a=int(m.group(6),16);k=bisect.bisect_right(bases,(a,'\xff'))-1
        mod=bases[k][1] if k>=0 else '?'; rva=a-bases[k][0] if k>=0 else a
        r=[float(m.group(1)),m.group(2).decode(),m.group(4).decode(),m.group(5).decode('latin-1'),mod,rva,None,m.group(3).decode()]
        recs.append(r);open_[(r[1],r[2])].append(r);continue
    m=retr.match(line)
    if m:
        st=open_[(m.group(2).decode(),m.group(4).decode())]
        if st: st.pop()[6]=int(m.group(5),16)
        continue
    m=tn.match(line)
    if m: tnames[(m.group(2) or m.group(1)).decode()]=m.group(3).decode('latin-1');continue
    if b'threadname' in line:
        raw.append(line[:170]);continue
    if b'loaddll' in line:
        m=load.search(line)
        if m: bisect.insort(bases,(int(m.group(2),16),m.group(1).decode('latin-1').lower().replace('.dll','')))
socks={};nsock=[0];hosts={}
def S(x): return socks.get(x,'h%x'%x)
def h(x): return int(x.strip().split(' ')[0],16)
def sg(x):
    x&=0xffffffff
    return x-(1<<32) if x>=0x80000000 else x
def host(n):
    if n is None: return 'NULL'
    l=n.lower()
    if l=='localhost' or l=='' or re.match(r'^127\.\d+\.\d+\.\d+$',l) or l in('::1','::','0.0.0.0') or l.startswith('::ffff:127.'): return '"%s"'%n
    kind='address' if (re.match(r'^[0-9.]+$',l) or ':' in l) else 'host'
    if n not in hosts: hosts[n]='<%s %d>'%(kind,len(hosts)+1)
    return hosts[n]
ga=re.compile(r'^([0-9a-f]+)(?: "([^"]*)")?,([0-9a-f]+)(?: "([^"]*)")?,([0-9a-f]+),([0-9a-f]+)$')
ev=[]
for t,tid,fn,args,mod,rva,rv,dll in recs:
    by=None;rs='?' if rv is None else str(sg(rv))
    try:
        a=args.split(',') if args else []
        if mod in EA: na='(not shown)';rs=''
        elif fn=='socket':
            na='%d,%d,%d'%(h(a[0]),h(a[1]),h(a[2]))
            if rv is not None and sg(rv)!=-1:
                nsock[0]+=1;socks[rv&0xffffffff]='S%d'%nsock[0];rs='S%d'%nsock[0]
        elif fn=='WSAStartup': na='0x%04x'%(h(a[0])&0xffff)
        elif fn in('closesocket','getsockname','getpeername','__WSAFDIsSet','shutdown','listen','accept'):
            na=S(h(a[0]))+(',%d'%h(a[1]) if fn in('shutdown','listen') else '')
            if fn=='closesocket': socks.pop(h(a[0]),None)
        elif fn=='ioctlsocket': na='%s,0x%08x'%(S(h(a[0])),h(a[1]))
        elif fn=='WSAIoctl': na='%s,0x%08x,inlen %d,outlen %d'%(S(h(a[0])),h(a[1]),h(a[3]),h(a[5]))
        elif fn=='setsockopt': na='%s,0x%x,0x%x,len %d'%(S(h(a[0])),h(a[1]),h(a[2]),h(a[4]))
        elif fn=='getsockopt': na='%s,0x%x,0x%x'%(S(h(a[0])),h(a[1]),h(a[2]))
        elif fn in('bind','connect'): na='%s,len %d'%(S(h(a[0])),h(a[2]))
        elif fn in('htons','ntohs','htonl','ntohl'): na='%d'%h(a[0])
        elif fn=='WSASetLastError': na='%d'%h(a[0]);rs=''
        elif fn=='inet_ntop': na='af %d,size %d'%(h(a[0]),h(a[3]));rs='NULL' if rv==0 else 'buf'
        elif fn in('inet_pton','InetPtonW'): na='af %d'%h(a[0])
        elif fn=='gethostname': na='len %d'%h(a[1])
        elif fn in('getaddrinfo','GetAddrInfoW','gethostbyname'):
            m=ga.match(args)
            if m: na='%s,%s,hints %s'%(host(m.group(2)),'NULL' if m.group(4) is None else '"%s"'%m.group(4),'NULL' if int(m.group(5),16)==0 else 'given')
            else: na='(unparsed)'
        elif fn=='freeaddrinfo': na='NULL' if h(a[0])==0 else 'list';rs=''
        elif fn=='recvfrom':
            na='%s,len %d,flags %x,from %s'%(S(h(a[0])),h(a[2]),h(a[3]),'NULL' if h(a[4])==0 else 'given')
            if rv is not None and sg(rv)>0: by=sg(rv);rs='n'
        elif fn=='recv':
            na='%s,flags %x'%(S(h(a[0])),h(a[3]))
            if rv is not None and sg(rv)>0: by=sg(rv);rs='n'
        elif fn=='send':
            na='%s,flags %x'%(S(h(a[0])),h(a[3]));by=h(a[2])
            if rv is not None and sg(rv)==by: rs='len'
        elif fn=='sendto':
            na='%s,flags %x,tolen %d'%(S(h(a[0])),h(a[3]),h(a[5]));by=h(a[2])
            if rv is not None and sg(rv)==by: rs='len'
        elif fn=='select': na='%s%s%s,%s'%('r' if h(a[1]) else '-','w' if h(a[2]) else '-','e' if h(a[3]) else '-','timeout' if h(a[4]) else 'no limit')
        elif fn=='GetAdaptersInfo': na='buf %s'%('NULL' if h(a[0])==0 else 'given');rs=str(rv)
        elif fn in('WSAGetLastError','WSACleanup'): na=''
        else: na='%d args'%len(a)
    except Exception as e: na='(parse: %s)'%type(e).__name__
    ev.append((t,tid,'%s+0x%x'%(mod,rva),fn,na,rs,by,mod,dll))
freq=collections.Counter(e[1:6] for e in ev)
if MODE=='counts':
    c=collections.Counter((e[8],e[3],e[7]) for e in ev)
    for k in sorted(c): print('%6d %s.%s from %s'%(c[k],k[0],k[1],k[2]))
    c=collections.Counter((e[1],e[7]) for e in ev)
    for k in sorted(c): print('%6d thread %s (%s) from %s'%(c[k],k[0],tnames.get(k[0],'?'),k[1]))
    print('distinct names asked of getaddrinfo:',len(hosts),sorted(set(v.split(' ')[0] for v in hosts.values())))
    print('unpaired calls:',sum(1 for e in ev if e[5]=='?'))
    sys.exit()
if MODE=='phases':
    PH=[(0,'A before the menu video'),(1467911.797,'B menu'),(1467920.166,'C mission load, play, quit')]
    for i,(p0,nm) in enumerate(PH):
        p1=PH[i+1][0] if i+1<len(PH) else 9e18
        d=collections.OrderedDict()
        for e in ev:
            if not(p0<=e[0]<p1): continue
            if i==0 and e[0]<1467909.9: continue
            v=d.setdefault(e[1:6],[0,e[0],e[0],None,None,0]);v[0]+=1;v[2]=e[0]
            if e[6] is not None:
                v[3]=e[6] if v[3] is None else min(v[3],e[6]);v[4]=e[6] if v[4] is None else max(v[4],e[6]);v[5]+=e[6]
        print('==',nm,len(d),'keys')
        for k,v in d.items():
            b='' if v[3] is None else ' bytes %d..%d sum %d'%(v[3],v[4],v[5])
            print('%6.2f-%6.2f %s %s %s(%s)=%s x%d%s'%(v[1]-T0,v[2]-T0,k[0],k[1],k[2],k[3],k[4],v[0],b))
    c=collections.Counter()
    for e in ev:
        if e[3]=='sendto' or (e[3]=='recvfrom' and e[5]=='n'): c[(int((e[0]-T0)//20)*20,e[3],e[4].split(',')[0])]+=1
    print('per 20 s:',' '.join('%d:%s:%s:%d'%(k[0],k[1],k[2],c[k]) for k in sorted(c)))
    sys.exit()
out=[];win=collections.OrderedDict();mi=[0]
def flush():
    for k,v in win.items():
        b='' if v[3] is None else ' bytes %d..%d sum %d'%(v[3],v[4],v[5])
        out.append('%7.2f-%7.2f %s %s %s(%s)=%s x%d%s'%(v[1]-T0,v[2]-T0,k[0],k[1],k[2],k[3],k[4],v[0],b))
    win.clear()
for e in ev:
    t=e[0]
    while mi[0]<len(MARK) and t>=MARK[mi[0]][0]:
        flush();out.append('---- %.2f %s'%(MARK[mi[0]][0]-T0,MARK[mi[0]][1]));mi[0]+=1
    k=e[1:6]
    if freq[k]>=6:
        v=win.get(k)
        if v is None: v=win[k]=[0,t,t,None,None,0]
        v[0]+=1;v[2]=t
        if e[6] is not None:
            v[3]=e[6] if v[3] is None else min(v[3],e[6]);v[4]=e[6] if v[4] is None else max(v[4],e[6]);v[5]+=e[6]
    else:
        flush();out.append('%7.2f %s %s %s(%s)=%s%s'%(t-T0,e[1],e[2],e[3],e[4],e[5],'' if e[6] is None else ' bytes %d'%e[6]))
flush()
print('lines',len(out),'threads',' '.join('%s=%s'%(k,v) for k,v in sorted(tnames.items()) if any(e[1]==k for e in ev)))
if not tnames:
    for l in raw[:4]: print(l)
out=[l for l in out if l.startswith('----') or float(l[:7])>=TMIN]
print('after TMIN',len(out))
for l in out[LO:HI]: print(l)
