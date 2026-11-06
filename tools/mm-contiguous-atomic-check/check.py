#!/usr/bin/env python3
"""Compile actual main-kernel FrameAlloc source in a controlled host test."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools/mm-contiguous-source-repro"))
from run import extract_mm
MM=ROOT/"crates/warden-kernel/src/mm.rs"
HARNESS=r'''
use std::alloc::{alloc_zeroed,dealloc,Layout};
struct Backing { ptr:*mut u8, layout:Layout }
impl Backing {
 fn new()->Self {
  let layout=Layout::from_size_align(4096*16,4096).unwrap();
  let ptr=unsafe{alloc_zeroed(layout)};
  assert!(!ptr.is_null());
  Self{ptr,layout}
 }
 fn fa(&self)->FrameAlloc {
  let x=self.ptr as u64; FrameAlloc::new(x,x+4096*16)
 }
}
impl Drop for Backing {fn drop(&mut self){unsafe{dealloc(self.ptr,self.layout)}}}
fn main(){
 let b=Backing::new();
 let base=b.ptr as u64;
 let mut z=b.fa();
 assert_eq!(z.alloc_contiguous(0),None);
 assert_eq!((z.used,z.high_water()),(0,0));
 assert_eq!(z.alloc_contiguous(u64::MAX),None);
 assert_eq!((z.used,z.high_water()),(0,0));
 let mut normal=b.fa();
 assert_eq!(normal.alloc_contiguous(3),Some(base));
 assert_eq!((normal.used,normal.high_water()),(3,3));
 let mut fragmented=b.fa();
 let a=fragmented.alloc().unwrap();
 let _b=fragmented.alloc().unwrap();
 let c=fragmented.alloc().unwrap();
 fragmented.free(a);
 fragmented.free(c);
 let old_used=fragmented.used;
 assert_eq!(old_used,1);
 let result=fragmented.alloc_contiguous(2).expect("fresh contiguous span");
 assert_eq!(result,base+3*PAGE);
 assert_eq!(fragmented.used,old_used+2);
 // The free-list remains available for ordinary allocations.
 assert_eq!(fragmented.alloc(),Some(c));
 assert_eq!(fragmented.alloc(),Some(a));
 let mut skip=b.fa();
 skip.reserve(base+2*PAGE,base+4*PAGE);
 assert_eq!(skip.alloc_contiguous(3),Some(base+4*PAGE));
 assert_eq!(skip.high_water(),7);
 assert_eq!(skip.used,3);
 let mut exhausted=b.fa();
 assert_eq!(exhausted.alloc_contiguous(15),Some(base));
 let before=(exhausted.used,exhausted.high_water());
 assert_eq!(exhausted.alloc_contiguous(2),None);
 assert_eq!((exhausted.used,exhausted.high_water()),before);
 println!("PASS actual FrameAlloc zero, overflow, fresh span, fragmented list, reserve, exhaustion");
}
'''
def main():
    source=extract_mm(MM.read_text())
    with tempfile.TemporaryDirectory(prefix="warden-contiguous-atomic-") as d:
        src=Path(d)/"check.rs"
        exe=Path(d)/"check"
        src.write_text("#![allow(dead_code)]\n"+source+"\n"+HARNESS)
        subprocess.run(["rustc","--edition=2021","-O",str(src),"-o",str(exe)],check=True,timeout=90)
        subprocess.run([str(exe)],check=True,timeout=15)
if __name__=="__main__":
    main()
