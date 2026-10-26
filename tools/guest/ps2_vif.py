import struct
def vif_codes(b, i, end):
    """VIF codes and their data in b[i:end]: yields (name, num, imm, data_offset, data_size, cmd)"""
    end=min(end,len(b))
    while i+4 <= end:
        w=struct.unpack_from('<I',b,i)[0]; cmd=(w>>24)&0x7f; num=(w>>16)&0xff; imm=w&0xffff
        if cmd>=0x60:
            vn=(cmd>>2)&3; vl=cmd&3; n=num or 256
            bits = 16 if vl==3 else (vn+1)*(32>>vl)
            size=(bits*n+31)//32*4
            if i+4+size > end: return
            yield ('UNPACK', n, imm, i+4, size, cmd); i+=4+size; continue
        if cmd==0x20: yield ('STMASK',0,imm,i+4,4,cmd); i+=8; continue
        if cmd in (0x30,0x31): yield ('STROW' if cmd==0x30 else 'STCOL',0,imm,i+4,16,cmd); i+=20; continue
        if cmd in (0x50,0x51): n=imm or 65536; yield ('DIRECT',n,imm,i+4,n*16,cmd); i+=4+n*16; continue
        if cmd==0x4a: n=num or 256; yield ('MPG',n,imm,i+4,n*8,cmd); i+=4+n*8; continue
        names={0:'NOP',1:'STCYCL',2:'OFFSET',3:'BASE',4:'ITOP',5:'STMOD',6:'MSKPATH3',7:'MARK',0x10:'FLUSHE',0x11:'FLUSH',0x13:'FLUSHA',0x14:'MSCAL',0x15:'MSCALF',0x17:'MSCNT'}
        yield (names.get(cmd,f'?{cmd:#x}'),num,imm,i,0,cmd); i+=4
def dma_chain(b, i, end):
    """(tag id, qwc, offset of the 2 VIF words, payload start, payload end) for each DMA tag"""
    while i+16 <= end:
        t0=struct.unpack_from('<I',b,i)[0]; qwc=t0&0xffff; tid=(t0>>28)&7
        if i+16+qwc*16 > end: return
        yield tid, qwc, i+8, i+16, i+16+qwc*16
        i += 16+qwc*16
        if tid==7: break   # END
