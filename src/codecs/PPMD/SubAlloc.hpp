/****************************************************************************
 *  This file is part of PPMd project                                       *
 *  Written and distributed to public domain by Dmitry Shkarin 1997,        *
 *  1999-2001                                                               *
 *  Contents: memory allocation routines                                    *
 ****************************************************************************/

enum { UNIT_SIZE=12, N1=4, N2=4, N3=4, N4=(128+3-1*N1-2*N2-3*N3)/4,
        N_INDEXES=N1+N2+N3+N4 };

/* FreeArc-ex: 32-bit references. The allocator links heap blocks through these
 * fields, so they must be 4 bytes like the model references; the heap is mapped
 * below 4 GiB (see fa_alloc_low). */
template <class T>
struct _PACK_ATTR fa_ptr32 {
    DWORD addr;
    T* get() const { return (T*)(uintptr_t)addr; }
    T* operator->() const { return get(); }
    operator T*() const { return get(); }
    fa_ptr32& operator=(T* p)
    {
#ifdef FA_PPMD_CHECK_PTR32
        if (p && ((uintptr_t)p >> 32) != 0) {
            fprintf(stderr, "PPMd: pointer outside the 32-bit arena: %p\n", (void*)p);
            abort();
        }
#endif
        addr = (DWORD)(uintptr_t)p;
        return *this;
    }
};

#pragma pack(1)
static _THREAD1 struct BLK_NODE {
    DWORD Stamp;
    fa_ptr32<BLK_NODE> next;
    BOOL   avail()      const { return (next != NULL); }
    void    link(BLK_NODE* p) { p->next=next; next=p; }
    void  unlink()            { next=next->next; }
    void* remove()            {
        BLK_NODE* p=next;                   unlink();
        Stamp--;                            return p;
    }
    inline void insert(void* pv,int NU);
} _THREAD BList[N_INDEXES];
struct MEM_BLK: public BLK_NODE { DWORD NU; } _PACK_ATTR;
#pragma pack()

static BYTE Indx2Units[N_INDEXES], Units2Indx[128]; // constants
static _THREAD1 DWORD _THREAD GlueCount, _THREAD SubAllocatorSize=0;
static _THREAD1 BYTE* _THREAD HeapStart, * _THREAD pText, * _THREAD UnitsStart, * _THREAD LoUnit, * _THREAD HiUnit;

inline void PrefetchData(void* Addr)
{
#if defined(_USE_PREFETCHING)
    BYTE PrefetchByte = *(volatile BYTE*) Addr;
#endif /* defined(_USE_PREFETCHING) */
}
inline void BLK_NODE::insert(void* pv,int NU) {
    MEM_BLK* p=(MEM_BLK*) pv;               link(p);
    p->Stamp=~0UL;                          p->NU=NU;
    Stamp++;
}
inline UINT U2B(UINT NU) { return 8*NU+4*NU; }
inline void SplitBlock(void* pv,UINT OldIndx,UINT NewIndx)
{
    UINT i, k, UDiff=Indx2Units[OldIndx]-Indx2Units[NewIndx];
    BYTE* p=((BYTE*) pv)+U2B(Indx2Units[NewIndx]);
    if (Indx2Units[i=Units2Indx[UDiff-1]] != UDiff) {
        k=Indx2Units[--i];                  BList[i].insert(p,k);
        p += U2B(k);                        UDiff -= k;
    }
    BList[Units2Indx[UDiff-1]].insert(p,UDiff);
}
DWORD _STDCALL GetUsedMemory()
{
    DWORD i, RetVal=SubAllocatorSize-(HiUnit-LoUnit)-(UnitsStart-pText);
    for (i=0;i < N_INDEXES;i++)
            RetVal -= UNIT_SIZE*Indx2Units[i]*BList[i].Stamp;
    return RetVal;
}
/* FreeArc-ex: the PPMd model stores 32-bit pointers, so on 64-bit targets the
 * heap must be addressable with 32 bits. It is mapped at a fixed low address;
 * if that fails, the fallback is checked against the 4 GiB limit. */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define FA_PPMD_LOW_HEAP 1
#endif

#ifdef FA_PPMD_LOW_HEAP
#if defined(_WIN32)
static BYTE* fa_alloc_low(UINT size)
{
    return (BYTE*) VirtualAlloc((void*)(uintptr_t)0x10000000, size,
                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
static void fa_free_low(BYTE* p, UINT) { if (p) VirtualFree(p, 0, MEM_RELEASE); }
#else
static BYTE* fa_alloc_low(UINT size)
{
    void* p = mmap((void*)(uintptr_t)0x10000000, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED) return (BYTE*)p;
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if ((uintptr_t)p + size > (uintptr_t)0x100000000ULL) { munmap(p, size); return NULL; }
    return (BYTE*)p;
}
static void fa_free_low(BYTE* p, UINT size) { if (p) munmap(p, size); }
#endif
#else
static BYTE* fa_alloc_low(UINT size) { return (BYTE*) BigAlloc(size); }
static void fa_free_low(BYTE* p, UINT size) { if (p) BigFree(p); }
#endif

void _STDCALL StopSubAllocator() {
    if ( SubAllocatorSize ) {
        fa_free_low(HeapStart, SubAllocatorSize);
        SubAllocatorSize=0;
    }
}
BOOL _STDCALL StartSubAllocator(UINT t)
{
    if (SubAllocatorSize == t)              return TRUE;
    StopSubAllocator();
    HeapStart = fa_alloc_low(t);
    if (HeapStart == NULL)                  return FALSE;
    SubAllocatorSize=t;                     return TRUE;
}
static inline void InitSubAllocator()
{
    memset(BList,0,sizeof(BList));
    HiUnit=(pText=HeapStart)+SubAllocatorSize;
    UINT Diff=UNIT_SIZE*(SubAllocatorSize/8/UNIT_SIZE*7);
    LoUnit=UnitsStart=HiUnit-Diff;          GlueCount=0;
}
static void GlueFreeBlocks()
{
    UINT i, k, sz;
    MEM_BLK s0, * p, * p0, * p1;
    if (LoUnit != HiUnit)                   *LoUnit=0;
    for (i=0, (p0=&s0)->next=NULL;i < N_INDEXES;i++)
            while ( BList[i].avail() ) {
                p=(MEM_BLK*) BList[i].remove();
                if ( !p->NU )               continue;
                while ((p1=p+p->NU)->Stamp == ~0UL) {
                    p->NU += p1->NU;        p1->NU=0;
                }
                p0->link(p);                p0=p;
            }
    while ( s0.avail() ) {
        p=(MEM_BLK*) s0.remove();           sz=p->NU;
        if ( !sz )                          continue;
        for ( ;sz > 128;sz -= 128, p += 128)
                BList[N_INDEXES-1].insert(p,128);
        if (Indx2Units[i=Units2Indx[sz-1]] != sz) {
            k=sz-Indx2Units[--i];           BList[k-1].insert(p+(sz-k),k);
        }
        BList[i].insert(p,Indx2Units[i]);
    }
    GlueCount=1 << 13;
}
static void* _STDCALL AllocUnitsRare(UINT indx)
{
    UINT i=indx;
    if ( !GlueCount ) {
        GlueFreeBlocks();
        if ( BList[i].avail() )             return BList[i].remove();
    }
    do {
        if (++i == N_INDEXES) {
            GlueCount--;                    i=U2B(Indx2Units[indx]);
            return (UnitsStart-pText > i)?(UnitsStart -= i):(NULL);
        }
    } while ( !BList[i].avail() );
    void* RetVal=BList[i].remove();         SplitBlock(RetVal,i,indx);
    return RetVal;
}
inline void* AllocUnits(UINT NU)
{
    UINT indx=Units2Indx[NU-1];
    if ( BList[indx].avail() )              return BList[indx].remove();
    void* RetVal=LoUnit;                    LoUnit += U2B(Indx2Units[indx]);
    if (LoUnit <= HiUnit)                   return RetVal;
    LoUnit -= U2B(Indx2Units[indx]);        return AllocUnitsRare(indx);
}
inline void* AllocContext()
{
    if (HiUnit != LoUnit)                   return (HiUnit -= UNIT_SIZE);
    else if ( BList->avail() )              return BList->remove();
    else                                    return AllocUnitsRare(0);
}
inline void UnitsCpy(void* Dest,void* Src,UINT NU)
{
    DWORD* p1=(DWORD*) Dest, * p2=(DWORD*) Src;
    do {
        p1[0]=p2[0];                        p1[1]=p2[1];
        p1[2]=p2[2];
        p1 += 3;                            p2 += 3;
    } while ( --NU );
}
inline void* ExpandUnits(void* OldPtr,UINT OldNU)
{
    UINT i0=Units2Indx[OldNU-1], i1=Units2Indx[OldNU-1+1];
    if (i0 == i1)                           return OldPtr;
    void* ptr=AllocUnits(OldNU+1);
    if ( ptr ) {
        UnitsCpy(ptr,OldPtr,OldNU);         BList[i0].insert(OldPtr,OldNU);
    }
    return ptr;
}
inline void* ShrinkUnits(void* OldPtr,UINT OldNU,UINT NewNU)
{
    UINT i0=Units2Indx[OldNU-1], i1=Units2Indx[NewNU-1];
    if (i0 == i1)                           return OldPtr;
    if ( BList[i1].avail() ) {
        void* ptr=BList[i1].remove();       UnitsCpy(ptr,OldPtr,NewNU);
        BList[i0].insert(OldPtr,Indx2Units[i0]);
        return ptr;
    } else {
        SplitBlock(OldPtr,i0,i1);           return OldPtr;
    }
}
inline void FreeUnits(void* ptr,UINT NU) {
    UINT indx=Units2Indx[NU-1];
    BList[indx].insert(ptr,Indx2Units[indx]);
}
inline void SpecialFreeUnit(void* ptr)
{
    if ((BYTE*) ptr != UnitsStart)          BList->insert(ptr,1);
    else { *(DWORD*) ptr=~0UL;              UnitsStart += UNIT_SIZE; }
}
inline void* MoveUnitsUp(void* OldPtr,UINT NU)
{
    UINT indx=Units2Indx[NU-1];
    if ((BYTE*) OldPtr > UnitsStart+16*1024 || (BLK_NODE*) OldPtr > BList[indx].next)
            return OldPtr;
    void* ptr=BList[indx].remove();
    UnitsCpy(ptr,OldPtr,NU);                NU=Indx2Units[indx];
    if ((BYTE*) OldPtr != UnitsStart)       BList[indx].insert(OldPtr,NU);
    else                                    UnitsStart += U2B(NU);
    return ptr;
}
static inline void ExpandTextArea()
{
    BLK_NODE* p;
    UINT Count[N_INDEXES];                  memset(Count,0,sizeof(Count));
    while ((p=(BLK_NODE*) UnitsStart)->Stamp == ~0UL) {
        MEM_BLK* pm=(MEM_BLK*) p;           UnitsStart=(BYTE*) (pm+pm->NU);
        Count[Units2Indx[pm->NU-1]]++;      pm->Stamp=0;
    }
    for (UINT i=0;i < N_INDEXES;i++)
        for (p=BList+i;Count[i] != 0;p=p->next)
            while ( !p->next->Stamp ) {
                p->unlink();                BList[i].Stamp--;
                if ( !--Count[i] )          break;
            }
}
