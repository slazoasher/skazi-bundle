from __future__ import annotations
import math, os, struct, wave
from array import array

SR=22050
DURATION=4.0
N=int(SR*DURATION)
TAU=2*math.pi
OUT=os.path.join(os.path.dirname(__file__), '..', 'docs', 'audio')
os.makedirs(OUT,exist_ok=True)

# Each preview is a small original sound-design sketch built from synthesis,
# not audio rendered from the SKAZI plugins themselves.
def env(t,d,a=.006,r=.08):
    if t<0 or t>=d: return 0.0
    return min(1.0,t/max(a,.0001))*min(1.0,(d-t)/max(r,.0001))

def osc(phase,kind):
    if kind=='sine': return math.sin(phase)
    if kind=='triangle': return 2/math.pi*math.asin(math.sin(phase))
    if kind=='square': return 1.0 if math.sin(phase)>=0 else -1.0
    # Slightly rounded saw keeps the generated previews bright without alias-heavy edges.
    return (2/math.pi)*sum(math.sin(phase*k)/k for k in range(1,8))

class Clip:
    def __init__(self):
        self.l=array('f',[0.0])*N; self.r=array('f',[0.0])*N
    def voice(self,f,start,d,amp=.2,kind='saw',to=None,attack=.006,release=.08,pan=0,cut=0,detune=0,shape=0):
        i0=max(0,int(start*SR)); i1=min(N,int((start+d)*SR)); phase=0.0
        pan=max(-1,min(1,pan)); gl=math.sqrt((1-pan)*.5); gr=math.sqrt((1+pan)*.5)
        for i in range(i0,i1):
            t=i/SR-start; progress=t/d
            freq=f*((to/f)**progress) if to and f>0 else f
            freq*=1+detune
            phase+=TAU*freq/SR
            val=osc(phase,kind)
            # A compact brightness envelope: harmonic-rich at note onset, darker at the tail.
            if cut:
                brightness=max(.12,min(1,(cut*(.42+.58*(1-progress if shape else progress)))/12000))
                val=math.tanh(val*(1+brightness*2.5))
            self.l[i]+=val*env(t,d,attack,release)*amp*gl
            self.r[i]+=val*env(t,d,attack,release)*amp*gr
    def kick(self,start,amp=.32):
        i0=max(0,int(start*SR)); i1=min(N,int((start+.48)*SR)); phase=0.0
        for i in range(i0,i1):
            t=i/SR-start; phase+=TAU*(43+128*math.exp(-t*28))/SR
            body=math.sin(phase)*math.exp(-t*7)
            click=(math.sin(TAU*2200*t)+.45*math.sin(TAU*3600*t))*math.exp(-t*95)
            v=(body+.12*click)*amp
            self.l[i]+=v; self.r[i]+=v
    def noise(self,start,d,amp=.08,pan=0,sweep=0):
        i0=max(0,int(start*SR)); i1=min(N,int((start+d)*SR)); seed=(int(start*100000)+1247)&0xffffffff
        gl=math.sqrt((1-pan)*.5); gr=math.sqrt((1+pan)*.5)
        prev=0.0
        for i in range(i0,i1):
            t=i/SR-start; seed=(1664525*seed+1013904223)&0xffffffff
            x=((seed/4294967295)*2-1)
            # Moving low-pass average for a smoother riser/glitch texture.
            blend=min(.95,max(.03,(sweep*(t/d)+80)/SR))
            prev=prev*(1-blend)+x*blend
            v=prev*env(t,d,.01,.13)*amp
            self.l[i]+=v*gl; self.r[i]+=v*gr
    def add_drums(self,step=.5,offset=0,amp=.7):
        t=offset
        while t<DURATION:
            self.kick(t,amp); t+=step
    def finish(self,name,stereo_width=0):
        peak=0.0
        for x in self.l: peak=max(peak,abs(x))
        for x in self.r: peak=max(peak,abs(x))
        gain=min(1.0,.88/max(peak,.0001))
        path=os.path.join(OUT,name+'.wav')
        with wave.open(path,'wb') as w:
            w.setnchannels(2);w.setsampwidth(2);w.setframerate(SR)
            data=bytearray()
            # A little width from a quiet opposite-channel time offset.
            delay=int(.012*SR)
            for i in range(N):
                l=self.l[i];r=self.r[i]
                if stereo_width:
                    l+=self.r[max(0,i-delay)]*stereo_width
                    r+=self.l[max(0,i-delay)]*stereo_width
                l=math.tanh(l*gain*1.2)/math.tanh(1.2)
                r=math.tanh(r*gain*1.2)/math.tanh(1.2)
                data.extend(struct.pack('<hh',int(max(-1,min(1,l))*32767),int(max(-1,min(1,r))*32767)))
            w.writeframes(data)
    
def build_wide():
    c=Clip(); c.kick(.05,.38); c.kick(1.05,.38); c.kick(2.05,.38); c.kick(3.05,.38)
    for t,root in [(0,110),(1,130.8),(2,98),(3,110)]:
        for f,p in [(root,-.2),(root*1.5,.15),(root*2,.45)]:
            c.voice(f,t,.78,.18,'saw',to=f*.997,release=.18,pan=p,detune=(-.003 if p<0 else .003),cut=2400,shape=1)
    for t in [.5,1.5,2.5,3.5]: c.voice(880,t,.12,.08,'sine',to=660,pan=.4)
    return c

def build_kick():
    c=Clip()
    for t in [.06,.56,1.06,1.56,2.06,2.56,3.06,3.56]: c.kick(t,.68 if t%1<.1 else .48)
    # Short snare-like transient adds the click and body the synth is designed for.
    for t in [.56,1.56,2.56,3.56]: c.noise(t,.055,.12,pan=.05,sweep=1800)
    return c

def build_psy():
    c=Clip(); c.add_drums(.5,.0,.22)
    notes=[55,55,65.4,73.4,55,82.4,73.4,65.4,55,55,65.4,98,73.4,65.4,55,49]
    for i,f in enumerate(notes):
        t=.08+i*.235
        c.voice(f,t,.17,.24,'saw',to=f*.985,release=.045,cut=3500 if i%4==0 else 2100,shape=1)
        if i%2==1: c.voice(f*2,t+.015,.12,.08,'square',to=f*1.9,pan=.25,release=.04,cut=1800,shape=1)
    return c

def build_sub():
    c=Clip()
    notes=[55,55,73.4,55,82.4,73.4,55,49]
    for i,f in enumerate(notes):
        t=.04+i*.48; c.voice(f,t,.4,.52,'sine',to=f*.92,release=.08)
        if i%2==0: c.kick(t,.24)
    return c

def build_riser():
    c=Clip(); c.noise(.03,3.55,.4,pan=.05,sweep=13000)
    c.voice(82,.08,3.4,.24,'saw',to=660,attack=.7,release=.35,cut=8000,shape=1)
    for i in range(16):
        t=.35+i*.205; c.noise(t,.075,.045,pan=(-.5 if i%2 else .5),sweep=5000)
    c.kick(3.67,.6)
    return c

def build_glitch():
    c=Clip(); seq=[440,660,330,880,494,740,370,990,440,587,330,784,523,880,392,660]
    for i,f in enumerate(seq):
        t=.08+i*.235; d=.055 if i%3 else .12
        c.voice(f,t,d,.3,'square' if i%2 else 'saw',to=f*(.7 if i%4==0 else 1.04),release=.025,pan=(-.7 if i%2 else .7),cut=4500,shape=1)
        if i%4==2: c.noise(t,.045,.1,pan=.4,sweep=7000)
    return c

def build_pump():
    c=Clip()
    for beat in range(8):
        t=.04+beat*.48; c.kick(t,.45)
        c.voice([110,164.8,130.8,196][beat%4],t+.09,.32,.38,'sine',release=.16)
        c.voice([220,261.6,196,293.7][beat%4],t+.10,.31,.12,'triangle',pan=.2,release=.14)
    return c

def build_echo():
    c=Clip(); notes=[440,523.3,659.3,587.3,392,493.9,587.3,523.3]
    for i,f in enumerate(notes):
        t=.12+i*.43
        for rep in range(4): c.voice(f,t+rep*.31,.16,.22/(rep+1),'triangle',to=f*.995,pan=(-.62 if rep%2 else .62),release=.1,cut=5000)
    return c

def build_drive():
    c=Clip(); c.add_drums(.48,.0,.32)
    for beat,root in enumerate([55,55,65.4,49,55,73.4,65.4,49]):
        t=.06+beat*.48
        for i,m in enumerate([1,1.5,2]): c.voice(root*m,t,.39,.2/(i+1),'saw',release=.1,pan=(-.55+i*.5),cut=1200,shape=1)
    return c

def build_filter():
    c=Clip()
    for i in range(8):
        t=.08+i*.47; f=[110,164.8,196,146.8][i%4]
        c.voice(f,t,.37,.24,'triangle',to=f*(1.02 if i%2 else .96),pan=(-.35 if i%2 else .35),release=.12,cut=4500,shape=1)
        c.noise(t+.03,.3,.075,pan=(.55 if i%2 else -.55),sweep=1800)
    return c

def build_acid():
    c=Clip(); c.add_drums(.5,.0,.23)
    notes=[55,55,65.4,73.4,55,82.4,73.4,65.4,49,49,65.4,73.4,82.4,73.4,65.4,55]
    for i,f in enumerate(notes):
        t=.08+i*.235; d=.19 if i%4 else .28
        c.voice(f,t,d,.38,'saw',to=notes[(i+1)%len(notes)] if i%4==3 else f*.99,release=.04,cut=4200 if i%3==0 else 1900,shape=1)
        if i%4==0: c.voice(f*2,t,.11,.09,'square',release=.025,pan=.25,cut=2400,shape=1)
    return c

def build_stutter():
    c=Clip(); c.noise(1.1,2.5,.22,pan=.15,sweep=9000)
    for i in range(32):
        t=.07+i*.115
        if i%8 in [0,4,6,7]:
            f=[330,247,392,294][(i//2)%4]
            c.voice(f,t,.07 if i%4 else .1,.22,'square' if i%2 else 'saw',to=f*(.86 if i%4==3 else 1.03),pan=(-.5 if i%2 else .5),release=.025,cut=3800,shape=1)
    c.voice(110,.18,3.25,.13,'saw',to=440,attack=1.0,release=.4,cut=6200,shape=1)
    c.kick(3.56,.52)
    return c

def build_arp():
    c=Clip(); c.add_drums(.5,.0,.25)
    scale=[261.6,329.6,392,523.3,392,329.6,293.7,349.2,440,587.3,440,349.2,246.9,329.6,392,493.9]
    for i,f in enumerate(scale):
        t=.08+i*.235
        c.voice(f,t,.19,.25,'sine',release=.08)
        c.voice(f*2,t,.13,.075,'triangle',pan=(.35 if i%2 else -.35),release=.06)
    return c

BUILDERS={
 'wide':build_wide,'kick':build_kick,'psy':build_psy,'sub':build_sub,
 'riser':build_riser,'glitch':build_glitch,'pump':build_pump,'echo':build_echo,
 'drive':build_drive,'filter':build_filter,'acid':build_acid,'stutter':build_stutter,'arp':build_arp,
}
for name,fn in BUILDERS.items():
    fn().finish(name, .11 if name in {'echo','drive','wide'} else 0)
    print(name)
