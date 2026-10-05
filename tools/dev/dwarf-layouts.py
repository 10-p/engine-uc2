#!/usr/bin/env python3
"""Print 'Type size N' and 'Type.member off N bit B/S' for every struct/class/union in the DWARF of the given
object files (any format llvm-dwarfdump reads: ELF, COFF, wasm), one fact per line, sorted, deduplicated.

    python3 tools/dev/dwarf-layouts.py build-a/Source/Engine/CMakeFiles/Engine.dir/Src/*.obj > a.txt

Used at M12 to compare two compiler layouts of the whole engine (MinGW -mms-bitfields vs -mno-ms-bitfields):
`diff` of two outputs lists every type whose size or member offsets differ. Run it inside bin/capped.
llvm-dwarfdump: $LLVM_DWARFDUMP, default the workspace's emsdk copy."""
import subprocess, sys, re
import os
DD=os.environ.get("LLVM_DWARFDUMP", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../emsdk/upstream/bin/llvm-dwarfdump"))
out=set()
for obj in sys.argv[1:]:
    p=subprocess.Popen([DD,"--debug-info",obj],stdout=subprocess.PIPE,text=True,errors="replace")
    stack=[]  # (depth, kind, name)
    cur=None
    for line in p.stdout:
        m=re.match(r'^0x[0-9a-f]+:(\s*)(DW_TAG_\w+)',line)
        if m:
            depth=len(m.group(1)); tag=m.group(2)
            while stack and stack[-1][0]>=depth: stack.pop()
            cur={'tag':tag,'depth':depth,'name':None,'size':None,'off':None,'bitoff':None,'bitsize':None,'decl':False}
            stack.append((depth,cur))
            continue
        if cur is None: continue
        a=re.match(r'\s+(DW_AT_\w+)\s+\((.*)\)\s*$',line)
        if not a: 
            if line.strip()=="" and cur:
                c=cur
                if c['tag'] in('DW_TAG_structure_type','DW_TAG_class_type','DW_TAG_union_type') and c['name'] and c['size'] is not None and not c['decl']:
                    out.add(f"{c['name']} size {c['size']}")
                if c['tag']=='DW_TAG_member' and c['name'] and len(stack)>=2:
                    par=stack[-2][1]
                    if par['tag'] in('DW_TAG_structure_type','DW_TAG_class_type','DW_TAG_union_type') and par['name'] and not par['decl']:
                        out.add(f"{par['name']}.{c['name']} off {c['off']} bit {c['bitoff']}/{c['bitsize']}")
            continue
        k,v=a.group(1),a.group(2)
        if k=='DW_AT_name': cur['name']=v.strip('"')
        elif k=='DW_AT_byte_size': cur['size']=v
        elif k=='DW_AT_data_member_location': cur['off']=v
        elif k=='DW_AT_data_bit_offset': cur['bitoff']=v
        elif k=='DW_AT_bit_offset': cur['bitoff']=v
        elif k=='DW_AT_bit_size': cur['bitsize']=v
        elif k=='DW_AT_declaration': cur['decl']=True
    p.wait()
for l in sorted(out): print(l)
