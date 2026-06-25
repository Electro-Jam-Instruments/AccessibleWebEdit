import sys, zlib, struct
def read_ppm(p):
    d=open(p,'rb').read()
    assert d[:2]==b'P6'
    # parse header tokens
    idx=2; vals=[]
    while len(vals)<3:
        while idx<len(d) and d[idx] in b' \t\n\r': idx+=1
        s=idx
        while idx<len(d) and d[idx] not in b' \t\n\r': idx+=1
        vals.append(int(d[s:idx]))
    idx+=1  # single whitespace after maxval
    w,h,mx=vals
    return w,h,d[idx:idx+w*h*3]
def write_png(p,w,h,rgb):
    raw=bytearray()
    for y in range(h):
        raw.append(0)
        raw+=rgb[y*w*3:(y+1)*w*3]
    def chunk(t,data):
        c=t+data
        return struct.pack('>I',len(data))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'
    png+=chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
    png+=chunk(b'IDAT',zlib.compress(bytes(raw),9))
    png+=chunk(b'IEND',b'')
    open(p,'wb').write(png)
w,h,rgb=read_ppm(sys.argv[1]); write_png(sys.argv[2],w,h,rgb)
print("wrote",sys.argv[2],w,"x",h)
