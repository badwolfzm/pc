// warden_bench.c — memory-bandwidth and render-stage microbenchmarks for the Warden/MatrixOS GPU design.
// Rules (per MatrixOS baseline harness): warmup runs, repeated runs, median and p95, timer floor measured,
// every output consumed into a hash, evidence id per row. Single-core rows are pinned with taskset by run.sh.
// Build: gcc -O2 -fopenmp -march=native warden_bench.c -o warden_bench   (march=native is for the harness
// only; it measures the machine, it is not a reproducible product build.)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include <omp.h>
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int cmp(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return x<y?-1:x>y;}
static uint64_t g_hash=0xcbf29ce484222325ull;
static void consume(uint64_t v){g_hash^=v;g_hash*=0x100000001b3ull;}
static uint64_t timer_floor(void){uint64_t best=UINT64_MAX;for(int i=0;i<2000;i++){uint64_t a=now_ns(),b=now_ns();if(b-a<best)best=b-a;}return best;}
static FILE *csv; static uint64_t tf; static int threads;
#define RUNS 11
#define WARM 2
// row: metric name, bytes moved per run (0 if not a bandwidth row), work units label
static void row(const char*ev,const char*metric,const char*unit,uint64_t*s,double bytes,double units){
  qsort(s,RUNS,8,cmp); uint64_t med=s[RUNS/2],p95=s[(RUNS*95)/100>=RUNS?RUNS-1:(RUNS*95)/100];
  double val = bytes>0 ? bytes/med : (units>0 ? units/med*1e9 : med/1e6);
  fprintf(csv,"%s,%s,%d,%d,%llu,%llu,%llu,%.3f,%s\n",ev,metric,RUNS,WARM,(unsigned long long)med,(unsigned long long)p95,(unsigned long long)tf,val,unit);
  printf("%-36s median %10.3f ms  p95 %10.3f ms  -> %.2f %s\n",metric,med/1e6,p95/1e6,val,unit);
}
#define BENCH(...) for(int r=-WARM;r<RUNS;r++){uint64_t t0=now_ns(); __VA_ARGS__; uint64_t t1=now_ns(); if(r>=0)s[r]=t1-t0;}
int main(int argc,char**argv){
  const char*out=argc>1?argv[1]:"results.csv"; const char*part=argc>2?argv[2]:"all"; int all=!strcmp(part,"all");
  csv=fopen(out,all?"w":"a"); if(all||!strcmp(part,"mem")) fprintf(csv,"evidence_id,metric,runs,warmup_runs,median_ns,p95_ns,timer_floor_ns,value,unit\n");
  tf=timer_floor(); threads=omp_get_max_threads(); uint64_t s[RUNS];
  printf("timer floor %llu ns, omp threads %d\n",(unsigned long long)tf,threads);
  // --- clock
  if(all||!strcmp(part,"mem")){ uint64_t x=0; long n=1000000000L; BENCH(for(long i=0;i<n;i++) __asm__ volatile("add $1,%0":"+r"(x))); consume(x);
    row("EV-BENCH-CLK-001","core_clock_dep_add_chain","GHz",s,0,(double)n/1e9); }
  // --- STREAM-like on DRAM arrays (default 512 MiB, BENCH_MIB env overrides), threads = OMP_NUM_THREADS
  if(all||!strcmp(part,"mem")){ size_t mib=getenv("BENCH_MIB")?atol(getenv("BENCH_MIB")):512; size_t n=(mib<<20)/8; double *a=aligned_alloc(64,n*8),*b=aligned_alloc(64,n*8),*c=aligned_alloc(64,n*8);
    #pragma omp parallel for
    for(size_t i=0;i<n;i++){a[i]=1;b[i]=2;c[i]=0;}
    char ev[64],m[64];
    BENCH({_Pragma("omp parallel for") for(size_t i=0;i<n;i++) c[i]=a[i];}); consume((uint64_t)c[n/3]);
    snprintf(ev,64,"EV-BENCH-MEM-COPY-%dT",threads); snprintf(m,64,"dram_copy_512MiB_%dthr",threads); row(ev,m,"GB/s",s,2.0*n*8,0);
    double sum=0; BENCH({sum=0; _Pragma("omp parallel for reduction(+:sum)") for(size_t i=0;i<n;i++) sum+=a[i];}); consume((uint64_t)sum);
    snprintf(ev,64,"EV-BENCH-MEM-READ-%dT",threads); snprintf(m,64,"dram_read_512MiB_%dthr",threads); row(ev,m,"GB/s",s,1.0*n*8,0);
    BENCH({_Pragma("omp parallel for") for(size_t i=0;i<n;i++) b[i]=3.0;}); consume((uint64_t)b[n/5]);
    snprintf(ev,64,"EV-BENCH-MEM-WRITE-%dT",threads); snprintf(m,64,"dram_write_512MiB_%dthr",threads); row(ev,m,"GB/s",s,1.0*n*8,0);
    BENCH({_Pragma("omp parallel for") for(size_t i=0;i<n;i++) a[i]=b[i]+3.0*c[i];}); consume((uint64_t)a[n/7]);
    snprintf(ev,64,"EV-BENCH-MEM-TRIAD-%dT",threads); snprintf(m,64,"dram_triad_512MiB_%dthr",threads); row(ev,m,"GB/s",s,3.0*n*8,0);
    free(a);free(b);free(c); }
  if(threads>1 || (!all && !strcmp(part,"mem"))) { fclose(csv); return 0; }
  // --- cache tier read bandwidth and load-to-use latency
  if(all||!strcmp(part,"cache")){ size_t sizes[]={32u<<10,1u<<20,16u<<20,128u<<20,1024u<<20}; const char*names[]={"L1_32KiB","L2_1MiB","L3_16MiB","L3edge_128MiB","DRAM_1GiB"};
    size_t top=getenv("BENCH_MIB")?(size_t)atol(getenv("BENCH_MIB"))<<20:(1024u<<20); if(top<(256u<<20))top=256u<<20; sizes[4]=top; char*buf=aligned_alloc(64,top); memset(buf,1,top);
    for(int k=0;k<5;k++){ size_t sz=sizes[k]; size_t iters=(2048ull<<20)/sz; if(iters<2)iters=2; char ev[64],m[64];
#ifdef __AVX512F__
      __m512i acc=_mm512_setzero_si512();
      BENCH({for(size_t it=0;it<iters;it++) for(size_t i=0;i<sz;i+=128){acc=_mm512_add_epi64(acc,_mm512_load_si512(buf+i));acc=_mm512_add_epi64(acc,_mm512_load_si512(buf+i+64));}});
      consume(_mm512_reduce_add_epi64(acc));
#else
      __m256i acc0=_mm256_setzero_si256(),acc1=acc0,acc2=acc0,acc3=acc0;
      BENCH({for(size_t it=0;it<iters;it++) for(size_t i=0;i<sz;i+=128){acc0=_mm256_add_epi64(acc0,_mm256_load_si256((__m256i*)(buf+i)));acc1=_mm256_add_epi64(acc1,_mm256_load_si256((__m256i*)(buf+i+32)));acc2=_mm256_add_epi64(acc2,_mm256_load_si256((__m256i*)(buf+i+64)));acc3=_mm256_add_epi64(acc3,_mm256_load_si256((__m256i*)(buf+i+96)));}});
      { long long t4[4]; _mm256_storeu_si256((__m256i*)t4,_mm256_add_epi64(_mm256_add_epi64(acc0,acc1),_mm256_add_epi64(acc2,acc3))); consume(t4[0]+t4[1]+t4[2]+t4[3]); }
#endif
      snprintf(ev,64,"EV-BENCH-CACHE-BW-%d",k); snprintf(m,64,"read_bw_%s_1core",names[k]); row(ev,m,"GB/s",s,(double)sz*iters,0);
      size_t lines=sz/64; size_t*idx=malloc(lines*8); for(size_t i=0;i<lines;i++)idx[i]=i; srand(1);
      for(size_t i=lines-1;i>0;i--){size_t j=rand()%(i+1);size_t t=idx[i];idx[i]=idx[j];idx[j]=t;}
      uint64_t*lb=(uint64_t*)buf; for(size_t i=0;i<lines;i++)lb[idx[i]*8]=idx[(i+1)%lines]*8; free(idx);
      size_t steps=5000000; uint64_t p=0; BENCH({for(size_t i=0;i<steps;i++)p=lb[p];}); consume(p);
      snprintf(ev,64,"EV-BENCH-CACHE-LAT-%d",k); snprintf(m,64,"latency_%s_1core",names[k]);
      qsort(s,RUNS,8,cmp); fprintf(csv,"%s,%s,%d,%d,%llu,%llu,%llu,%.2f,ns\n",ev,m,RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,(double)s[RUNS/2]/steps);
      printf("%-36s %.1f ns per dependent load\n",m,(double)s[RUNS/2]/steps); memset(buf,1,sz); }
    free(buf); }
  // --- peak FMA throughput, one core
#ifdef __AVX512F__
  if(all||!strcmp(part,"fma")){ __m512 a[8]; for(int i=0;i<8;i++)a[i]=_mm512_set1_ps(1+i); __m512 b=_mm512_set1_ps(1.000001f),c=_mm512_set1_ps(0.5f); long n=100000000L;
    BENCH({for(long i=0;i<n;i++) for(int k=0;k<8;k++) a[k]=_mm512_fmadd_ps(a[k],b,c);}); float f=0; for(int i=0;i<8;i++)f+=_mm512_reduce_add_ps(a[i]); consume((uint64_t)f);
    row("EV-BENCH-FMA-512","peak_fp32_avx512_fma_1core","GFLOP/s",s,0,(double)n*8*16*2/1e9); }
#endif
  if(all||!strcmp(part,"fma")){ __m256 a0=_mm256_set1_ps(1),a1=_mm256_set1_ps(2),a2=_mm256_set1_ps(3),a3=_mm256_set1_ps(4),a4=_mm256_set1_ps(5),a5=_mm256_set1_ps(6),a6=_mm256_set1_ps(7),a7=_mm256_set1_ps(8); __m256 b=_mm256_set1_ps(1.000001f),c=_mm256_set1_ps(0.5f); long n=100000000L;
    BENCH({for(long i=0;i<n;i++){a0=_mm256_fmadd_ps(a0,b,c);a1=_mm256_fmadd_ps(a1,b,c);a2=_mm256_fmadd_ps(a2,b,c);a3=_mm256_fmadd_ps(a3,b,c);a4=_mm256_fmadd_ps(a4,b,c);a5=_mm256_fmadd_ps(a5,b,c);a6=_mm256_fmadd_ps(a6,b,c);a7=_mm256_fmadd_ps(a7,b,c);}}); float t[8]; _mm256_storeu_ps(t,_mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a0,a1),_mm256_add_ps(a2,a3)),_mm256_add_ps(_mm256_add_ps(a4,a5),_mm256_add_ps(a6,a7)))); consume((uint64_t)t[0]);
    row("EV-BENCH-FMA-256","peak_fp32_avx2_fma_1core","GFLOP/s",s,0,(double)n*8*8*2/1e9); }
  // --- render stages at 720p RGBA
  enum{W=1280,H=720}; size_t fb=(size_t)W*H*4;
  if(all||!strcmp(part,"render")){ uint8_t*bg=aligned_alloc(64,fb),*fr=aligned_alloc(64,fb); memset(bg,0x80,fb); memset(fr,0,fb);
    int reps=200; BENCH({for(int i=0;i<reps;i++) memcpy(fr,bg,fb);}); consume(fr[777]);
    qsort(s,RUNS,8,cmp); fprintf(csv,"EV-BENCH-R-COMPOSITE-L3,composite_720p_memcpy_hot,%d,%d,%llu,%llu,%llu,%.4f,ms/frame\n",RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,s[RUNS/2]/1e6/reps);
    printf("%-36s %.3f ms/frame\n","composite_720p_memcpy_hot",s[RUNS/2]/1e6/reps);
    size_t big=512u<<20; uint8_t*pool=aligned_alloc(64,big); memset(pool,1,big); int slots=big/fb;
    BENCH({for(int i=0;i<reps;i++) memcpy(fr,pool+(size_t)(i%slots)*fb,fb);}); consume(fr[999]);
    qsort(s,RUNS,8,cmp); fprintf(csv,"EV-BENCH-R-COMPOSITE-DRAM,composite_720p_memcpy_from_dram,%d,%d,%llu,%llu,%llu,%.4f,ms/frame\n",RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,s[RUNS/2]/1e6/reps);
    printf("%-36s %.3f ms/frame\n","composite_720p_memcpy_from_dram",s[RUNS/2]/1e6/reps); free(pool);
    // alpha blend 200 sprites 128x128 (3.3 Mpix) AVX2 src-over
    int S=128,n=200; uint8_t*spr=aligned_alloc(64,S*S*4); for(int i=0;i<S*S*4;i++)spr[i]=(i*31)&255; int reps2=20;
    BENCH({for(int rr=0;rr<reps2;rr++) for(int sp_i=0;sp_i<n;sp_i++){ int ox=(sp_i*37)%(W-S),oy=(sp_i*53)%(H-S);
      for(int y=0;y<S;y++){ uint8_t*d=fr+((oy+y)*W+ox)*4,*sp=spr+y*S*4; for(int x=0;x<S;x+=8){
        __m256i sv=_mm256_loadu_si256((__m256i*)(sp+x*4)),dv=_mm256_loadu_si256((__m256i*)(d+x*4)),z=_mm256_setzero_si256(),k255=_mm256_set1_epi16(255);
        __m256i slo=_mm256_unpacklo_epi8(sv,z),shi=_mm256_unpackhi_epi8(sv,z),dlo=_mm256_unpacklo_epi8(dv,z),dhi=_mm256_unpackhi_epi8(dv,z);
        __m256i alo=_mm256_shufflehi_epi16(_mm256_shufflelo_epi16(slo,0xFF),0xFF),ahi=_mm256_shufflehi_epi16(_mm256_shufflelo_epi16(shi,0xFF),0xFF);
        __m256i rlo=_mm256_srli_epi16(_mm256_add_epi16(_mm256_mullo_epi16(slo,alo),_mm256_mullo_epi16(dlo,_mm256_sub_epi16(k255,alo))),8);
        __m256i rhi=_mm256_srli_epi16(_mm256_add_epi16(_mm256_mullo_epi16(shi,ahi),_mm256_mullo_epi16(dhi,_mm256_sub_epi16(k255,ahi))),8);
        _mm256_storeu_si256((__m256i*)(d+x*4),_mm256_packus_epi16(rlo,rhi)); }}}}); consume(fr[4321]);
    qsort(s,RUNS,8,cmp); fprintf(csv,"EV-BENCH-R-BLEND,alpha_blend_200x128x128_avx2,%d,%d,%llu,%llu,%llu,%.4f,ms/frame\n",RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,s[RUNS/2]/1e6/reps2);
    printf("%-36s %.3f ms/frame (%.1f Mpix)\n","alpha_blend_200x128x128_avx2",s[RUNS/2]/1e6/reps2,n*S*S/1e6);
    // per-pixel lambert shading, full 720p, 3 normal planes in, 1 out (AVX-512 only)
#ifdef __AVX512F__
    float*nx=aligned_alloc(64,W*H*4),*ny=aligned_alloc(64,W*H*4),*nz=aligned_alloc(64,W*H*4),*o=aligned_alloc(64,W*H*4);
    for(int i=0;i<W*H;i++){nx[i]=.3f;ny[i]=.4f;nz[i]=.866f;}
    __m512 lx=_mm512_set1_ps(.5f),ly=_mm512_set1_ps(.5f),lz=_mm512_set1_ps(.707f),amb=_mm512_set1_ps(.1f),kk=_mm512_set1_ps(.9f); int reps3=50;
    BENCH({for(int rr=0;rr<reps3;rr++) for(int i=0;i<W*H;i+=16){ __m512 d=_mm512_fmadd_ps(_mm512_load_ps(nx+i),lx,_mm512_fmadd_ps(_mm512_load_ps(ny+i),ly,_mm512_mul_ps(_mm512_load_ps(nz+i),lz))); d=_mm512_max_ps(d,_mm512_setzero_ps()); _mm512_store_ps(o+i,_mm512_fmadd_ps(d,kk,amb)); }}); consume((uint64_t)(o[5000]*1000));
    qsort(s,RUNS,8,cmp); fprintf(csv,"EV-BENCH-R-SHADE,lambert_720p_avx512_16B_per_px,%d,%d,%llu,%llu,%llu,%.4f,ms/frame\n",RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,s[RUNS/2]/1e6/reps3);
    printf("%-36s %.3f ms/frame\n","lambert_720p_avx512",s[RUNS/2]/1e6/reps3);
#endif
    // RGBA -> YUV420 conversion (what an encoder needs), scalar C, 720p
    uint8_t*yuv=aligned_alloc(64,W*H*3/2); int reps4=20;
    BENCH({for(int rr=0;rr<reps4;rr++){ for(int i=0;i<W*H;i++){uint8_t*p=bg+i*4; yuv[i]=(uint8_t)((66*p[0]+129*p[1]+25*p[2]+128)>>8)+16;}
      uint8_t*u=yuv+W*H,*v=u+W*H/4; for(int y=0;y<H;y+=2)for(int x=0;x<W;x+=2){uint8_t*p=bg+(y*W+x)*4; int j=(y/2)*(W/2)+x/2; u[j]=(uint8_t)(((-38*p[0]-74*p[1]+112*p[2]+128)>>8)+128); v[j]=(uint8_t)(((112*p[0]-94*p[1]-18*p[2]+128)>>8)+128);} }}); consume(yuv[123]^yuv[W*H+5]);
    qsort(s,RUNS,8,cmp); fprintf(csv,"EV-BENCH-R-CSC,rgba_to_yuv420_720p_scalar_c,%d,%d,%llu,%llu,%llu,%.4f,ms/frame\n",RUNS,WARM,(unsigned long long)s[RUNS/2],(unsigned long long)s[(RUNS*95)/100],(unsigned long long)tf,s[RUNS/2]/1e6/reps4);
    printf("%-36s %.3f ms/frame\n","rgba_to_yuv420_720p_scalar_c",s[RUNS/2]/1e6/reps4);
  }
  fprintf(csv,"EV-BENCH-HASH,consumed_output_hash,0,0,0,0,%llu,%llu,hash\n",(unsigned long long)tf,(unsigned long long)g_hash);
  printf("consumed hash 0x%016llx\n",(unsigned long long)g_hash); fclose(csv); return 0;
}
