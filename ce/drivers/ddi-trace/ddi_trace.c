#include <windows.h>
#include <winddi.h>

#define SETTINGS_KEY TEXT("Software\\Brain\\DdiTrace")
#define DEFAULT_DRIVER TEXT("ddraw_mx2_.dll")
#define DEFAULT_OUTPUT TEXT("\\Storage Card\\ddi-trace.txt")
#define STORAGE_FOLDER TEXT("\\Storage Card")
#define DEFAULT_WRAP_MASK 0x046AC03F
#define SLOT_COUNT 28
#define CALLBACK_COUNT 13
#define CALLBACK_COPY_WORDS 32
#define KEY_FIELDS 12
#define SHAPE_CAPACITY 2048
#define DUMP_CAPACITY 128
#define DUMP_WORDS 80
#define FIRST_WRITE_MILLISECONDS 3000
#define WRITE_INTERVAL_MILLISECONDS 10000
#define LOCK_ATTEMPTS 50
#define REPORT_BYTES (1024 * 1024)
#define NO_OBJECT 0xFFFFFFFF
#define CALLBACK_BASE 64
#define MARK_ESCAPE 0x44444900
#define PHASE_COUNT 256
#define COUNTED_SLOTS (CALLBACK_BASE + CALLBACK_COUNT)
#define EXTRA_SLOT_COUNT 3
#define EXTRA_SLOT_BASE 32
#define EXTRA_ARGUMENTS 14
#define EVENT_CAPACITY 128
#define ESCAPE_UNSUPPORTED_QUERY 6151
#define ESCAPE_GET_SCREEN_ROTATION 6302
#define EXTRA_CALLBACK_COUNT 3
#define EXTRA_CALLBACK_BASE 40
#define PROBE_BASE 96
#define PROBE_COUNT 4
#define PROBE_CLIP 0
#define PROBE_PATH 1
#define PROBE_BRUSH 2
#define PROBE_MASK 3
#define PROBE_CLIP_LIMIT 64
#define PROBE_PATH_LIMIT 64
#define PROBE_BRUSH_LIMIT 16
#define PROBE_DUMP_LIMIT 6
#define PROBE_ENUM_LIMIT 1024
#define PROBE_BIT_CLIP 0x01
#define PROBE_BIT_PATH 0x02
#define PROBE_BIT_BRUSH 0x04
#define PROBE_BIT_MASK 0x08
#define PROBE_BIT_PRIMARY 0x10
#define PROBE_BIT_ESCAPE 0x20
#define PROBE_BIT_EXTRA_CALLBACKS 0x40
#define PROBE_BIT_CLIP_WORDS 0x80
#define PROBE_BIT_PIXELS 0x100
#define MASK_SAMPLE_PHASE 30
#define MASK_SAMPLE_LIMIT 4
#define PIXEL_PHASE 20
#define PIXEL_DUMP_LIMIT 8
#define PROBE_CLIP_WORDS_LIMIT 6
#define PROBE_RECTS 8
#define PROBE_POINT_WORDS 16

typedef ULONG (*EXTRA_FUNCTION)(ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG);
typedef BOOL (*ENABLE_DRIVER_FUNCTION)(ULONG, ULONG, DRVENABLEDATA *, PENGCALLBACKS);
typedef ULONG (*HAL_INIT_FUNCTION)(ULONG, ULONG, ULONG, ULONG);

typedef struct
{
    ULONG Slot;
    ULONG FieldCount;
    ULONG Fields[KEY_FIELDS];
    DWORD Count;
} SHAPE;

typedef struct
{
    const char *pName;
    ULONG WordCount;
    ULONG Words[DUMP_WORDS];
} DUMP;

typedef struct
{
    char *pText;
    DWORD Length;
} REPORT;

typedef struct
{
    DWORD Tick;
    ULONG Slot;
    ULONG First;
    ULONG Second;
    ULONG Third;
} EVENT;

static const char *const s_SlotNames[SLOT_COUNT] = {
    "EnablePDEV", "DisablePDEV", "EnableSurface", "DisableSurface", "CreateDeviceBitmap", "DeleteDeviceBitmap",
    "RealizeBrush", "StrokePath", "FillPath", "Paint", "BitBlt", "CopyBits", "AnyBlt", "TransparentBlt",
    "SetPalette", "SetPointerShape", "MovePointer", "GetModes", "RealizeColor", "GetMasks", "UnrealizeColor",
    "ContrastControl", "PowerHandler", "EndDoc", "StartDoc", "StartPage", "Escape", "GradientFill",
};

static const char *const s_CallbackNames[CALLBACK_COUNT] = {
    "BRUSHOBJ_pvAllocRbrush", "BRUSHOBJ_pvGetRbrush", "CLIPOBJ_cEnumStart", "CLIPOBJ_bEnum", "PALOBJ_cGetColors",
    "PATHOBJ_vEnumStart", "PATHOBJ_bEnum", "PATHOBJ_vGetBounds", "XLATEOBJ_cGetPalette", "EngCreateDeviceSurface",
    "EngDeleteSurface", "EngCreateDeviceBitmap", "EngCreatePalette",
};

static const char *const s_ProbeNames[PROBE_COUNT] = {
    "probe clip", "probe path", "probe brush", "probe mask",
};

static const char *const s_ProbeFieldNames[PROBE_COUNT][KEY_FIELDS] = {
    { "enumStart", "rects", "outside", "enumCalls", "complete" },
    { "fill", "subpaths", "points", "flags", "enumCalls", "complete" },
    { "realized", "realizeCalls" },
    { "format", "rop4", "brush" },
};

static const char *const s_FieldNames[SLOT_COUNT][KEY_FIELDS] = {
    { "cjCaps", "cjDevInfo", "cPat", "bitsPerPel", "width", "height" },
    { 0 },
    { "returned" },
    { 0 },
    { "format", "cx", "cy" },
    { 0 },
    { "target", "pattern", "patternCx", "patternCy", "mask", "flXlate", "xlateTypes", "hatch" },
    { "surface", "clip", "brush", "mix", "lineFlags", "join", "endCap", "styles", "width", "pathFlags" },
    { "surface", "clip", "brush", "mix", "options", "pathFlags" },
    { "surface", "clip", "brush", "mix" },
    { "target", "source", "mask", "clip", "flXlate", "xlateTypes", "brush", "rop4", "sameSurface", "brushOrigin" },
    { "target", "source", "clip", "flXlate", "xlateTypes", "sameSurface" },
    { "target", "source", "mask", "clip", "flXlate", "xlateTypes", "brush", "rop4", "iMode", "bltFlags", "stretch", "halftoneOrigin" },
    { "target", "source", "clip", "flXlate", "xlateTypes", "stretch" },
    { "fl", "iStart", "cColors" },
    { "surface", "mask", "maskCx", "maskCy", "color", "fl", "returned" },
    { "hide" },
    { "cjSize", "devmode", "returned" },
    { "iDstType", "cEntries" },
    { 0 },
    { "iSrcType", "cEntries" },
    { "cmd" },
    { "bOff" },
    { "fl" },
    { 0 },
    { 0 },
    { "iEsc", "cjIn", "cjOut", "surface", "returned" },
    { "surface", "clip", "flXlate", "mode", "nVertex", "nMesh" },
};

static HMODULE s_hOriginal;
static TCHAR s_DriverName[MAX_PATH] = DEFAULT_DRIVER;
static TCHAR s_OutputPath[MAX_PATH] = DEFAULT_OUTPUT;
static DWORD s_WrapMask = DEFAULT_WRAP_MASK;
static DWORD s_WrapCallbacks;
static CRITICAL_SECTION s_Lock;
static ULONG s_EngineVersion;
static ULONG s_EnableDataSize;
static DRVENABLEDATA s_Original;
static ENGCALLBACKS s_OriginalCallbacks;
static PVOID s_CallbackCopy[CALLBACK_COPY_WORDS];
static ULONG s_CallbackWords;
static DWORD s_Calls[SLOT_COUNT];
static DWORD s_CallbackCalls[CALLBACK_COUNT];
static SHAPE s_Shapes[SHAPE_CAPACITY];
static DWORD s_ShapeCount;
static DWORD s_ShapeOverflow;
static DUMP s_Dumps[DUMP_CAPACITY];
static DWORD s_DumpCount;
static DWORD s_HalInitCalls;
static HAL_INIT_FUNCTION s_pHalInit;
static volatile DWORD s_Stage;
static volatile DWORD s_LastEntered = NO_OBJECT;
static volatile LONG s_Depth;
static volatile ULONG s_Phase;
static EXTRA_FUNCTION s_OriginalExtra[EXTRA_SLOT_COUNT];
static DWORD s_ExtraCalls[EXTRA_SLOT_COUNT];
static ULONG s_ExtraFirstArguments[EXTRA_SLOT_COUNT][EXTRA_ARGUMENTS];
static EVENT s_Events[EVENT_CAPACITY];
static DWORD s_EventCount;
static DWORD s_PhaseCalls[PHASE_COUNT][COUNTED_SLOTS];
static DWORD s_Probe;
static DWORD s_ClearExtraSlots;
static DWORD s_PixelDumps;
static DWORD s_MaskSampleDumps;
static ULONG s_MaskTable[256];
static DWORD s_MaskTableCounts[256];
static DWORD s_PixelBlitDumps;
static DWORD s_ProbeCalls[PROBE_COUNT];
static HSURF s_PrimarySurface;
static DHSURF s_PrimaryDeviceSurface;
static SURFOBJ s_SeenTargets[8];
static DWORD s_SeenTargetCount;
static SURFOBJ s_LastPrimary;
static DWORD s_RotationDumps;
static EXTRA_FUNCTION s_OriginalExtraCallbacks[EXTRA_CALLBACK_COUNT];
static DWORD s_ExtraCallbackCalls[EXTRA_CALLBACK_COUNT];
static ULONG s_ExtraCallbackFirstArguments[EXTRA_CALLBACK_COUNT][EXTRA_ARGUMENTS];

static ULONG SurfaceKey(const SURFOBJ *pso)
{
    if (!pso)
        return NO_OBJECT;
    return (pso->iBitmapFormat & 0xff) | ((ULONG)pso->iType << 8) | ((ULONG)pso->fjBitmap << 16);
}

static ULONG ClipKey(const CLIPOBJ *pco)
{
    if (!pco)
        return NO_OBJECT;
    return pco->iDComplexity | ((ULONG)pco->iFComplexity << 8) | ((ULONG)pco->iMode << 16);
}

static ULONG BrushKey(const BRUSHOBJ *pbo)
{
    if (!pbo)
        return NO_OBJECT;
    return pbo->iSolidColor == 0xFFFFFFFF ? 1 : 0;
}

static ULONG XlateFlags(const XLATEOBJ *pxlo)
{
    return pxlo ? pxlo->flXlate : NO_OBJECT;
}

static ULONG XlateTypes(const XLATEOBJ *pxlo)
{
    return pxlo ? pxlo->iSrcType | ((ULONG)pxlo->iDstType << 16) : NO_OBJECT;
}

static ULONG Stretched(const RECTL *pDest, const RECTL *pSource)
{
    LONG DestWidth, DestHeight, SourceWidth, SourceHeight;
    ULONG Result = 0;

    if (!pDest || !pSource)
        return NO_OBJECT;
    DestWidth = pDest->right - pDest->left;
    DestHeight = pDest->bottom - pDest->top;
    SourceWidth = pSource->right - pSource->left;
    SourceHeight = pSource->bottom - pSource->top;
    if (DestWidth != SourceWidth)
        Result |= DestWidth == -SourceWidth ? 4 : 1;
    if (DestHeight != SourceHeight)
        Result |= DestHeight == -SourceHeight ? 8 : 2;
    return Result;
}

static void Enter(ULONG Slot)
{
    s_LastEntered = Slot;
    InterlockedIncrement((LONG *)&s_Depth);
}

static void Leave(void)
{
    InterlockedDecrement((LONG *)&s_Depth);
}

static void AddShape(ULONG Slot, const ULONG *pFields, ULONG FieldCount)
{
    ULONG Hash = 2166136261u ^ Slot;
    ULONG Index, Probe;

    for (Index = 0; Index < FieldCount; Index++)
        Hash = (Hash ^ pFields[Index]) * 16777619u;
    EnterCriticalSection(&s_Lock);
    if (Slot < COUNTED_SLOTS)
        s_PhaseCalls[s_Phase][Slot]++;
    if (Slot < SLOT_COUNT)
        s_Calls[Slot]++;
    else if (Slot >= CALLBACK_BASE && Slot < CALLBACK_BASE + CALLBACK_COUNT)
        s_CallbackCalls[Slot - CALLBACK_BASE]++;
    else if (Slot >= PROBE_BASE && Slot < PROBE_BASE + PROBE_COUNT)
        s_ProbeCalls[Slot - PROBE_BASE]++;
    for (Probe = 0; Probe < SHAPE_CAPACITY; Probe++)
    {
        SHAPE *pShape = &s_Shapes[(Hash + Probe) % SHAPE_CAPACITY];

        if (!pShape->Count)
        {
            if (s_ShapeCount >= SHAPE_CAPACITY * 3 / 4)
            {
                s_ShapeOverflow++;
                break;
            }
            pShape->Slot = Slot;
            pShape->FieldCount = FieldCount;
            memcpy(pShape->Fields, pFields, FieldCount * sizeof(ULONG));
            pShape->Count = 1;
            s_ShapeCount++;
            break;
        }
        if (pShape->Slot == Slot && pShape->FieldCount == FieldCount && !memcmp(pShape->Fields, pFields, FieldCount * sizeof(ULONG)))
        {
            pShape->Count++;
            break;
        }
    }
    LeaveCriticalSection(&s_Lock);
}

static void AddEvent(ULONG Slot, ULONG First, ULONG Second, ULONG Third)
{
    EVENT *pEvent;

    EnterCriticalSection(&s_Lock);
    pEvent = &s_Events[s_EventCount % EVENT_CAPACITY];
    pEvent->Tick = GetTickCount();
    pEvent->Slot = Slot;
    pEvent->First = First;
    pEvent->Second = Second;
    pEvent->Third = Third;
    s_EventCount++;
    LeaveCriticalSection(&s_Lock);
}

static void AddDump(const char *pName, const void *pData, ULONG Bytes)
{
    DUMP *pDump;

    if (!pData)
        return;
    EnterCriticalSection(&s_Lock);
    if (s_DumpCount < DUMP_CAPACITY)
    {
        pDump = &s_Dumps[s_DumpCount++];
        pDump->pName = pName;
        pDump->WordCount = Bytes / sizeof(ULONG);
        if (pDump->WordCount > DUMP_WORDS)
            pDump->WordCount = DUMP_WORDS;
        memcpy(pDump->Words, pData, pDump->WordCount * sizeof(ULONG));
    }
    LeaveCriticalSection(&s_Lock);
}

static void DumpOnce(BOOL *pDone, const char *pName, const void *pData, ULONG Bytes)
{
    if (*pDone || !pData)
        return;
    *pDone = TRUE;
    AddDump(pName, pData, Bytes);
}

static BOOL ProbeReady(DWORD Bit)
{
    return (s_Probe & Bit) && s_WrapCallbacks && s_CallbackWords >= CALLBACK_COUNT;
}

static BOOL RectangleInside(const RECTL *pInner, const RECTL *pOuter)
{
    return pInner->left >= pOuter->left && pInner->top >= pOuter->top && pInner->right <= pOuter->right && pInner->bottom <= pOuter->bottom;
}

static DWORD s_ClipWordDumps;

static void ProbeClipWords(CLIPOBJ *pco)
{
    if (!(s_Probe & PROBE_BIT_CLIP_WORDS) || !pco || pco->iDComplexity != DC_COMPLEX || s_ClipWordDumps >= PROBE_CLIP_WORDS_LIMIT)
        return;
    s_ClipWordDumps++;
    AddDump("probe complex CLIPOBJ words", pco, 24 * sizeof(ULONG));
}

static void ProbeClip(CLIPOBJ *pco)
{
    struct
    {
        ULONG c;
        RECTL arcl[PROBE_RECTS];
    } Rectangles;
    ULONG Fields[5], Index, Count;
    BOOL bMore;

    if (!ProbeReady(PROBE_BIT_CLIP) || !pco || pco->iDComplexity != DC_COMPLEX || s_ProbeCalls[PROBE_CLIP] >= PROBE_CLIP_LIMIT)
        return;
    if (s_ProbeCalls[PROBE_CLIP] < PROBE_DUMP_LIMIT)
        AddDump("probe clip CLIPOBJ and following words", pco, 16 * sizeof(ULONG));
    Fields[0] = s_OriginalCallbacks.CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_ANY, 0);
    Fields[1] = 0;
    Fields[2] = 0;
    Fields[3] = 0;
    do
    {
        Rectangles.c = 0;
        bMore = s_OriginalCallbacks.CLIPOBJ_bEnum(pco, sizeof(Rectangles), (ULONG *)&Rectangles);
        Fields[3]++;
        Count = Rectangles.c < PROBE_RECTS ? Rectangles.c : PROBE_RECTS;
        for (Index = 0; Index < Count; Index++)
        {
            Fields[1]++;
            if (!RectangleInside(&Rectangles.arcl[Index], &pco->rclBounds))
                Fields[2]++;
        }
        if (s_ProbeCalls[PROBE_CLIP] < PROBE_DUMP_LIMIT && Fields[3] <= 2)
            AddDump("probe clip ENUMRECTS", &Rectangles, sizeof(ULONG) + Count * sizeof(RECTL));
    } while (bMore && Fields[3] < PROBE_ENUM_LIMIT);
    Fields[4] = !bMore;
    AddShape(PROBE_BASE + PROBE_CLIP, Fields, 5);
}

static void ProbePath(PATHOBJ *ppo, ULONG Fill)
{
    PATHDATA Data;
    RECTFX Bounds;
    ULONG Fields[6];
    BOOL bMore, bDump = s_ProbeCalls[PROBE_PATH] < PROBE_DUMP_LIMIT;

    if (!ProbeReady(PROBE_BIT_PATH) || !ppo || s_ProbeCalls[PROBE_PATH] >= PROBE_PATH_LIMIT)
        return;
    if (bDump)
    {
        AddDump(Fill ? "probe fill PATHOBJ words" : "probe stroke PATHOBJ words", ppo, 8 * sizeof(ULONG));
        s_OriginalCallbacks.PATHOBJ_vGetBounds(ppo, &Bounds);
        AddDump("probe path bounds RECTFX", &Bounds, sizeof(Bounds));
    }
    Fields[0] = Fill;
    Fields[1] = 0;
    Fields[2] = 0;
    Fields[3] = 0;
    Fields[4] = 0;
    s_OriginalCallbacks.PATHOBJ_vEnumStart(ppo);
    do
    {
        Data.flags = 0;
        Data.count = 0;
        Data.pptfx = NULL;
        bMore = s_OriginalCallbacks.PATHOBJ_bEnum(ppo, &Data);
        Fields[4]++;
        Fields[2] += Data.count;
        Fields[3] |= Data.flags;
        if (Data.flags & PD_BEGINSUBPATH)
            Fields[1]++;
        if (bDump && Fields[4] <= 2 && Data.pptfx && Data.count)
            AddDump("probe path PATHDATA points", Data.pptfx, (Data.count * 2 < PROBE_POINT_WORDS ? Data.count * 2 : PROBE_POINT_WORDS) * sizeof(ULONG));
    } while (bMore && Fields[4] < PROBE_ENUM_LIMIT);
    Fields[5] = !bMore;
    s_OriginalCallbacks.PATHOBJ_vEnumStart(ppo);
    AddShape(PROBE_BASE + PROBE_PATH, Fields, 6);
}

static void ProbeBrush(BRUSHOBJ *pbo)
{
    ULONG Fields[2];
    DWORD RealizeCalls;

    if (!ProbeReady(PROBE_BIT_BRUSH) || !pbo || pbo->iSolidColor != 0xFFFFFFFF || s_ProbeCalls[PROBE_BRUSH] >= PROBE_BRUSH_LIMIT)
        return;
    RealizeCalls = s_Calls[6];
    Fields[0] = s_OriginalCallbacks.BRUSHOBJ_pvGetRbrush(pbo) ? 1 : 0;
    Fields[1] = s_Calls[6] - RealizeCalls;
    AddShape(PROBE_BASE + PROBE_BRUSH, Fields, 2);
}

static void ProbeMask(SURFOBJ *psoMask, BRUSHOBJ *pbo, ROP4 rop4, const RECTL *prclTrg, const POINTL *pptlMask)
{
    ULONG Fields[3], RowBytes, Geometry[8];
    LONG Row;

    if (!(s_Probe & PROBE_BIT_MASK) || !psoMask || psoMask->iBitmapFormat == BMF_1BPP)
        return;
    if (s_Phase == MASK_SAMPLE_PHASE && psoMask->pvScan0 && prclTrg && s_MaskSampleDumps < MASK_SAMPLE_LIMIT)
    {
        LONG SampleRow, Height = prclTrg->bottom - prclTrg->top;
        ULONG SampleBytes = psoMask->lDelta < 0 ? (ULONG)-psoMask->lDelta : (ULONG)psoMask->lDelta;

        s_MaskSampleDumps++;
        Geometry[0] = (ULONG)prclTrg->left;
        Geometry[1] = (ULONG)prclTrg->top;
        Geometry[2] = (ULONG)prclTrg->right;
        Geometry[3] = (ULONG)prclTrg->bottom;
        Geometry[4] = pptlMask ? (ULONG)pptlMask->x : NO_OBJECT;
        Geometry[5] = pptlMask ? (ULONG)pptlMask->y : NO_OBJECT;
        Geometry[6] = pbo ? pbo->iSolidColor : NO_OBJECT;
        Geometry[7] = (ULONG)psoMask->lDelta;
        AddDump("sample mask rect, origin, brush, lDelta", Geometry, sizeof(Geometry));
        for (SampleRow = 0; SampleRow < Height && SampleRow < 16; SampleRow++)
            AddDump("sample mask row", (const BYTE *)psoMask->pvScan0 + SampleRow * psoMask->lDelta, SampleBytes < 80 ? SampleBytes : 80);
    }
    if (s_ProbeCalls[PROBE_MASK] >= PROBE_DUMP_LIMIT)
        return;
    Fields[0] = psoMask->iBitmapFormat;
    Fields[1] = rop4;
    Fields[2] = pbo ? pbo->iSolidColor : NO_OBJECT;
    AddShape(PROBE_BASE + PROBE_MASK, Fields, 3);
    AddDump("probe mask SURFOBJ", psoMask, sizeof(SURFOBJ));
    Geometry[0] = prclTrg ? (ULONG)prclTrg->left : NO_OBJECT;
    Geometry[1] = prclTrg ? (ULONG)prclTrg->top : NO_OBJECT;
    Geometry[2] = prclTrg ? (ULONG)prclTrg->right : NO_OBJECT;
    Geometry[3] = prclTrg ? (ULONG)prclTrg->bottom : NO_OBJECT;
    Geometry[4] = pptlMask ? (ULONG)pptlMask->x : NO_OBJECT;
    Geometry[5] = pptlMask ? (ULONG)pptlMask->y : NO_OBJECT;
    Geometry[6] = pbo ? pbo->iSolidColor : NO_OBJECT;
    Geometry[7] = rop4;
    AddDump("probe mask target rect, mask origin, brush, rop4", Geometry, sizeof(Geometry));
    if (psoMask->pvScan0 && prclTrg && prclTrg->bottom > prclTrg->top)
    {
        LONG Height = prclTrg->bottom - prclTrg->top;

        RowBytes = psoMask->lDelta < 0 ? (ULONG)-psoMask->lDelta : (ULONG)psoMask->lDelta;
        for (Row = Height / 3; Row < Height && Row < Height / 3 + 4; Row++)
            AddDump("probe mask row", (const BYTE *)psoMask->pvScan0 + Row * psoMask->lDelta, RowBytes < 64 ? RowBytes : 64);
    }
}

static void ProbeTarget(SURFOBJ *pso)
{
    DWORD Index;

    for (Index = 0; Index < s_SeenTargetCount; Index++)
    {
        if (s_SeenTargets[Index].dhsurf == pso->dhsurf && s_SeenTargets[Index].hsurf == pso->hsurf && s_SeenTargets[Index].pvScan0 == pso->pvScan0)
            return;
    }
    if (s_SeenTargetCount >= sizeof(s_SeenTargets) / sizeof(s_SeenTargets[0]))
        return;
    s_SeenTargets[s_SeenTargetCount++] = *pso;
    AddDump("probe distinct target SURFOBJ", pso, sizeof(SURFOBJ));
}

static void ProbePrimary(SURFOBJ *pso)
{
    if (!(s_Probe & PROBE_BIT_PRIMARY) || !pso)
        return;
    ProbeTarget(pso);
    if (pso->hsurf != s_PrimarySurface && (!s_PrimaryDeviceSurface || pso->dhsurf != s_PrimaryDeviceSurface))
        return;
    if (pso->sizlBitmap.cx == s_LastPrimary.sizlBitmap.cx && pso->sizlBitmap.cy == s_LastPrimary.sizlBitmap.cy &&
        pso->lDelta == s_LastPrimary.lDelta && pso->pvScan0 == s_LastPrimary.pvScan0 && pso->pvBits == s_LastPrimary.pvBits &&
        pso->cjBits == s_LastPrimary.cjBits && pso->dhsurf == s_LastPrimary.dhsurf)
        return;
    s_LastPrimary = *pso;
    if (s_RotationDumps++ < 12)
        AddDump("probe primary SURFOBJ changed", pso, sizeof(SURFOBJ));
}

static DHPDEV TraceEnablePDEV(DEVMODEW *pdm, LPWSTR pwszLogAddress, ULONG cPat, HSURF *phsurfPatterns, ULONG cjCaps, ULONG *pdevcaps,
                              ULONG cjDevInfo, DEVINFO *pdi, HDEV hdev, LPWSTR pwszDeviceName, HANDLE hDriver)
{
    DHPDEV Result;
    ULONG Fields[6];

    Enter(0);
    s_Stage = 4;
    Result = s_Original.DrvEnablePDEV(pdm, pwszLogAddress, cPat, phsurfPatterns, cjCaps, pdevcaps, cjDevInfo, pdi, hdev, pwszDeviceName, hDriver);
    Leave();
    s_Stage = 5;
    Fields[0] = cjCaps;
    Fields[1] = cjDevInfo;
    Fields[2] = cPat;
    Fields[3] = pdm ? pdm->dmBitsPerPel : NO_OBJECT;
    Fields[4] = pdm ? pdm->dmPelsWidth : NO_OBJECT;
    Fields[5] = pdm ? pdm->dmPelsHeight : NO_OBJECT;
    AddShape(0, Fields, 6);
    AddEvent(0, Fields[4], Fields[5], cjDevInfo);
    AddDump("EnablePDEV GDIINFO", pdevcaps, cjCaps);
    AddDump("EnablePDEV DEVINFO", pdi, cjDevInfo);
    AddDump("EnablePDEV DEVMODEW", pdm, pdm ? pdm->dmSize + pdm->dmDriverExtra : 0);
    return Result;
}

static VOID TraceDisablePDEV(DHPDEV dhpdev)
{
    Enter(1);
    AddShape(1, NULL, 0);
    s_Original.DrvDisablePDEV(dhpdev);
    Leave();
}

static HSURF TraceEnableSurface(DHPDEV dhpdev)
{
    HSURF Result;
    ULONG Field;

    Enter(2);
    Result = s_Original.DrvEnableSurface(dhpdev);
    Leave();
    s_PrimarySurface = Result;
    AddDump("DrvEnableSurface returned", &Result, sizeof(Result));
    s_Stage = 6;
    Field = Result ? 1 : 0;
    AddShape(2, &Field, 1);
    return Result;
}

static VOID TraceDisableSurface(DHPDEV dhpdev)
{
    Enter(3);
    AddShape(3, NULL, 0);
    s_Original.DrvDisableSurface(dhpdev);
    Leave();
}

static HBITMAP TraceCreateDeviceBitmap(DHPDEV dhpdev, SIZEL sizl, ULONG iFormat)
{
    HBITMAP Result;
    ULONG Fields[3];

    Fields[0] = iFormat;
    Fields[1] = sizl.cx;
    Fields[2] = sizl.cy;
    AddShape(4, Fields, 1);
    Enter(4);
    Result = s_Original.DrvCreateDeviceBitmap(dhpdev, sizl, iFormat);
    Leave();
    return Result;
}

static VOID TraceDeleteDeviceBitmap(DHSURF dhsurf)
{
    AddShape(5, NULL, 0);
    Enter(5);
    s_Original.DrvDeleteDeviceBitmap(dhsurf);
    Leave();
}

static BOOL TraceRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask, XLATEOBJ *pxlo, ULONG iHatch)
{
    static BOOL s_bDumped;
    BOOL Result;
    ULONG Fields[8];

    Fields[0] = SurfaceKey(psoTarget);
    Fields[1] = SurfaceKey(psoPattern);
    Fields[2] = psoPattern ? (ULONG)psoPattern->sizlBitmap.cx : NO_OBJECT;
    Fields[3] = psoPattern ? (ULONG)psoPattern->sizlBitmap.cy : NO_OBJECT;
    Fields[4] = SurfaceKey(psoMask);
    Fields[5] = XlateFlags(pxlo);
    Fields[6] = XlateTypes(pxlo);
    Fields[7] = iHatch;
    AddShape(6, Fields, 8);
    DumpOnce(&s_bDumped, "RealizeBrush pattern SURFOBJ", psoPattern, sizeof(SURFOBJ));
    Enter(6);
    Result = s_Original.DrvRealizeBrush(pbo, psoTarget, psoPattern, psoMask, pxlo, iHatch);
    Leave();
    return Result;
}

static BOOL TraceStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs,
                            MIX mix)
{
    static BOOL s_bDumped;
    BOOL Result;
    ULONG Fields[10];

    Fields[0] = SurfaceKey(pso);
    Fields[1] = ClipKey(pco);
    Fields[2] = BrushKey(pbo);
    Fields[3] = mix;
    Fields[4] = plineattrs ? plineattrs->fl : NO_OBJECT;
    Fields[5] = plineattrs ? plineattrs->iJoin : NO_OBJECT;
    Fields[6] = plineattrs ? plineattrs->iEndCap : NO_OBJECT;
    Fields[7] = plineattrs ? plineattrs->cstyle : NO_OBJECT;
    Fields[8] = plineattrs ? (ULONG)plineattrs->elWidth.l : NO_OBJECT;
    Fields[9] = ppo ? ppo->fl : NO_OBJECT;
    AddShape(7, Fields, 10);
    DumpOnce(&s_bDumped, "StrokePath LINEATTRS", plineattrs, sizeof(LINEATTRS));
    ProbePrimary(pso);
    ProbeClip(pco);
    ProbePath(ppo, 0);
    Enter(7);
    Result = s_Original.DrvStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
    Leave();
    return Result;
}

static BOOL TraceFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix, FLONG flOptions)
{
    BOOL Result;
    ULONG Fields[6];

    Fields[0] = SurfaceKey(pso);
    Fields[1] = ClipKey(pco);
    Fields[2] = BrushKey(pbo);
    Fields[3] = mix;
    Fields[4] = flOptions;
    Fields[5] = ppo ? ppo->fl : NO_OBJECT;
    AddShape(8, Fields, 6);
    ProbePrimary(pso);
    ProbeClip(pco);
    ProbePath(ppo, 1);
    Enter(8);
    Result = s_Original.DrvFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
    Leave();
    return Result;
}

static BOOL TracePaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix)
{
    BOOL Result;
    ULONG Fields[4];

    Fields[0] = SurfaceKey(pso);
    Fields[1] = ClipKey(pco);
    Fields[2] = BrushKey(pbo);
    Fields[3] = mix;
    AddShape(9, Fields, 4);
    Enter(9);
    Result = s_Original.DrvPaint(pso, pco, pbo, pptlBrushOrg, mix);
    Leave();
    return Result;
}

static BOOL TraceBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclTrg, POINTL *pptlSrc,
                        POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush, ROP4 rop4)
{
    static BOOL s_bDumpedTarget, s_bDumpedSource, s_bDumpedClip, s_bDumpedXlate, s_bDumpedBrush;
    BOOL Result;
    ULONG Fields[10];

    Fields[0] = SurfaceKey(psoTrg);
    Fields[1] = SurfaceKey(psoSrc);
    Fields[2] = SurfaceKey(psoMask);
    Fields[3] = ClipKey(pco);
    Fields[4] = XlateFlags(pxlo);
    Fields[5] = XlateTypes(pxlo);
    Fields[6] = BrushKey(pbo);
    Fields[7] = rop4;
    Fields[8] = psoSrc && psoSrc == psoTrg;
    Fields[9] = pptlBrush ? 1 : 0;
    AddShape(10, Fields, 10);
    if (psoTrg && psoTrg->iType == STYPE_DEVICE)
        DumpOnce(&s_bDumpedTarget, "BitBlt device target SURFOBJ", psoTrg, sizeof(SURFOBJ));
    if (psoSrc && psoSrc->iType == STYPE_BITMAP)
        DumpOnce(&s_bDumpedSource, "BitBlt bitmap source SURFOBJ", psoSrc, sizeof(SURFOBJ));
    if (pco && pco->iDComplexity == DC_COMPLEX)
        DumpOnce(&s_bDumpedClip, "BitBlt complex CLIPOBJ", pco, sizeof(CLIPOBJ));
    DumpOnce(&s_bDumpedXlate, "BitBlt XLATEOBJ", pxlo, sizeof(XLATEOBJ));
    DumpOnce(&s_bDumpedBrush, "BitBlt BRUSHOBJ", pbo, sizeof(BRUSHOBJ));
    ProbePrimary(psoTrg);
    ProbeClipWords(pco);
    ProbeClip(pco);
    ProbeBrush(pbo);
    ProbeMask(psoMask, pbo, rop4, prclTrg, pptlMask);
    if ((s_Probe & PROBE_BIT_PIXELS) && s_Phase == PIXEL_PHASE && s_PixelBlitDumps < 4)
    {
        ULONG Words[6];

        Words[0] = rop4;
        Words[1] = XlateFlags(pxlo);
        Words[2] = XlateTypes(pxlo);
        Words[3] = SurfaceKey(psoSrc);
        Words[4] = prclTrg ? (ULONG)(prclTrg->right - prclTrg->left) : NO_OBJECT;
        Words[5] = prclTrg ? (ULONG)(prclTrg->bottom - prclTrg->top) : NO_OBJECT;
        s_PixelBlitDumps++;
        AddDump("probe pixel-phase BitBlt rop4 flXlate types source width height", Words, sizeof(Words));
        AddDump("probe pixel-phase BitBlt target SURFOBJ", psoTrg, sizeof(SURFOBJ));
    }
    Enter(10);
    Result = s_Original.DrvBitBlt(psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4);
    Leave();
    if ((s_Probe & PROBE_BIT_MASK) && s_Phase == MASK_SAMPLE_PHASE && psoMask && psoMask->iBitmapFormat == BMF_8BPP && psoMask->pvScan0 &&
        psoTrg && psoTrg->iBitmapFormat == BMF_32BPP && psoTrg->pvScan0 && prclTrg && !pptlMask)
    {
        LONG X, Y;

        for (Y = prclTrg->top; Y < prclTrg->bottom; Y++)
        {
            const BYTE *pMaskRow = (const BYTE *)psoMask->pvScan0 + (Y - prclTrg->top) * psoMask->lDelta;
            const ULONG *pTargetRow = (const ULONG *)((const BYTE *)psoTrg->pvScan0 + Y * psoTrg->lDelta);

            for (X = prclTrg->left; X < prclTrg->right; X++)
            {
                BYTE Coverage = pMaskRow[X - prclTrg->left];

                if (!s_MaskTableCounts[Coverage])
                    s_MaskTable[Coverage] = pTargetRow[X] & 0x00FFFFFF;
                else if (s_MaskTable[Coverage] != (pTargetRow[X] & 0x00FFFFFF))
                    s_MaskTable[Coverage] |= 0x80000000;
                s_MaskTableCounts[Coverage]++;
            }
        }
    }
    return Result;
}

static BOOL TraceCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc)
{
    BOOL Result;
    ULONG Fields[6];

    Fields[0] = SurfaceKey(psoDest);
    Fields[1] = SurfaceKey(psoSrc);
    Fields[2] = ClipKey(pco);
    Fields[3] = XlateFlags(pxlo);
    Fields[4] = XlateTypes(pxlo);
    Fields[5] = psoSrc && psoSrc == psoDest;
    AddShape(11, Fields, 6);
    Enter(11);
    Result = s_Original.DrvCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
    Leave();
    return Result;
}

static BOOL TraceAnyBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, POINTL *pptlHTOrg, RECTL *prclDest,
                        RECTL *prclSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush, ROP4 rop4, ULONG iMode, ULONG bltFlags)
{
    static BOOL s_bDumped;
    BOOL Result;
    ULONG Fields[12];

    Fields[0] = SurfaceKey(psoDest);
    Fields[1] = SurfaceKey(psoSrc);
    Fields[2] = SurfaceKey(psoMask);
    Fields[3] = ClipKey(pco);
    Fields[4] = XlateFlags(pxlo);
    Fields[5] = XlateTypes(pxlo);
    Fields[6] = BrushKey(pbo);
    Fields[7] = rop4;
    Fields[8] = iMode;
    Fields[9] = bltFlags;
    Fields[10] = Stretched(prclDest, prclSrc);
    Fields[11] = pptlHTOrg ? 1 : 0;
    AddShape(12, Fields, 12);
    if (Fields[10] && Fields[10] != NO_OBJECT && !s_bDumped)
    {
        RECTL Rectangles[2];

        Rectangles[0] = *prclDest;
        Rectangles[1] = *prclSrc;
        DumpOnce(&s_bDumped, "AnyBlt stretch destination and source RECTL", Rectangles, sizeof(Rectangles));
    }
    Enter(12);
    Result = s_Original.DrvAnyBlt(psoDest, psoSrc, psoMask, pco, pxlo, pptlHTOrg, prclDest, prclSrc, pptlMask, pbo, pptlBrush, rop4, iMode, bltFlags);
    Leave();
    return Result;
}

static BOOL TraceTransparentBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, RECTL *prclSrc, ULONG TransColor)
{
    BOOL Result;
    ULONG Fields[6];

    Fields[0] = SurfaceKey(psoDest);
    Fields[1] = SurfaceKey(psoSrc);
    Fields[2] = ClipKey(pco);
    Fields[3] = XlateFlags(pxlo);
    Fields[4] = XlateTypes(pxlo);
    Fields[5] = Stretched(prclDest, prclSrc);
    AddShape(13, Fields, 6);
    Enter(13);
    Result = s_Original.DrvTransparentBlt(psoDest, psoSrc, pco, pxlo, prclDest, prclSrc, TransColor);
    Leave();
    return Result;
}

static BOOL TraceSetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl, ULONG iStart, ULONG cColors)
{
    BOOL Result;
    ULONG Fields[3];

    Fields[0] = fl;
    Fields[1] = iStart;
    Fields[2] = cColors;
    AddShape(14, Fields, 3);
    Enter(14);
    Result = s_Original.DrvSetPalette(dhpdev, ppalo, fl, iStart, cColors);
    Leave();
    return Result;
}

static ULONG TraceSetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo, LONG xHot, LONG yHot, LONG x, LONG y,
                                  RECTL *prcl, FLONG fl)
{
    ULONG Result;
    ULONG Fields[7];

    Enter(15);
    Result = s_Original.DrvSetPointerShape(pso, psoMask, psoColor, pxlo, xHot, yHot, x, y, prcl, fl);
    Leave();
    Fields[0] = SurfaceKey(pso);
    Fields[1] = SurfaceKey(psoMask);
    Fields[2] = psoMask ? (ULONG)psoMask->sizlBitmap.cx : NO_OBJECT;
    Fields[3] = psoMask ? (ULONG)psoMask->sizlBitmap.cy : NO_OBJECT;
    Fields[4] = SurfaceKey(psoColor);
    Fields[5] = fl;
    Fields[6] = Result;
    AddShape(15, Fields, 7);
    return Result;
}

static VOID TraceMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl)
{
    ULONG Field = x == -1;

    AddShape(16, &Field, 1);
    Enter(16);
    s_Original.DrvMovePointer(pso, x, y, prcl);
    Leave();
}

static ULONG TraceGetModes(HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm)
{
    ULONG Result;
    ULONG Fields[3];

    Enter(17);
    s_Stage = 3;
    Result = s_Original.DrvGetModes(hDriver, cjSize, pdm);
    Leave();
    Fields[0] = cjSize;
    Fields[1] = pdm ? 1 : 0;
    Fields[2] = Result;
    AddShape(17, Fields, 3);
    if (pdm && Result)
        AddDump("GetModes DEVMODEW", pdm, Result < cjSize ? Result : cjSize);
    return Result;
}

static ULONG TraceRealizeColor(USHORT iDstType, ULONG cEntries, ULONG *pPalette, ULONG rgbColor)
{
    ULONG Result;
    ULONG Fields[2];

    Fields[0] = iDstType;
    Fields[1] = cEntries;
    AddShape(18, Fields, 2);
    Enter(18);
    Result = s_Original.DrvRealizeColor(iDstType, cEntries, pPalette, rgbColor);
    Leave();
    return Result;
}

static ULONG *TraceGetMasks(DHPDEV dhpdev)
{
    static BOOL s_bDumped;
    ULONG *pResult;

    Enter(19);
    pResult = s_Original.DrvGetMasks(dhpdev);
    Leave();
    AddShape(19, NULL, 0);
    DumpOnce(&s_bDumped, "GetMasks", pResult, 3 * sizeof(ULONG));
    return pResult;
}

static ULONG TraceUnrealizeColor(USHORT iSrcType, ULONG cEntries, ULONG *pPalette, ULONG iRealizedColor)
{
    ULONG Result;
    ULONG Fields[2];

    Fields[0] = iSrcType;
    Fields[1] = cEntries;
    AddShape(20, Fields, 2);
    Enter(20);
    Result = s_Original.DrvUnrealizeColor(iSrcType, cEntries, pPalette, iRealizedColor);
    Leave();
    if ((s_Probe & PROBE_BIT_PIXELS) && s_Phase == PIXEL_PHASE && s_PixelDumps < PIXEL_DUMP_LIMIT)
    {
        ULONG Words[8];
        ULONG Index;

        memset(Words, 0, sizeof(Words));
        Words[0] = iSrcType;
        Words[1] = cEntries;
        Words[2] = iRealizedColor;
        Words[3] = Result;
        for (Index = 0; Index < cEntries && Index < 4 && pPalette; Index++)
            Words[4 + Index] = pPalette[Index];
        s_PixelDumps++;
        AddDump("probe UnrealizeColor type entries realized result palette0-3", Words, sizeof(Words));
    }
    return Result;
}

static BOOL TraceContrastControl(DHPDEV dhpdev, ULONG cmd, ULONG *pValue)
{
    BOOL Result;

    AddShape(21, &cmd, 1);
    AddEvent(21, cmd, pValue ? *pValue : NO_OBJECT, 0);
    Enter(21);
    Result = s_Original.DrvContrastControl(dhpdev, cmd, pValue);
    Leave();
    return Result;
}

static VOID TracePowerHandler(DHPDEV dhpdev, BOOL bOff)
{
    ULONG Field = (ULONG)bOff;

    AddShape(22, &Field, 1);
    AddEvent(22, Field, 0, 0);
    Enter(22);
    s_Original.DrvPowerHandler(dhpdev, bOff);
    Leave();
}

static BOOL TraceEndDoc(SURFOBJ *pso, FLONG fl)
{
    BOOL Result;

    AddShape(23, &fl, 1);
    Enter(23);
    Result = s_Original.DrvEndDoc(pso, fl);
    Leave();
    return Result;
}

static BOOL TraceStartDoc(SURFOBJ *pso, PWSTR pwszDocName, DWORD dwJobId)
{
    BOOL Result;

    AddShape(24, NULL, 0);
    Enter(24);
    Result = s_Original.DrvStartDoc(pso, pwszDocName, dwJobId);
    Leave();
    return Result;
}

static BOOL TraceStartPage(SURFOBJ *pso)
{
    BOOL Result;

    AddShape(25, NULL, 0);
    Enter(25);
    Result = s_Original.DrvStartPage(pso);
    Leave();
    return Result;
}

static ULONG TraceEscape(DHPDEV dhpdev, SURFOBJ *pso, ULONG iEsc, ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut)
{
    ULONG Result;
    ULONG Fields[5];

    if (iEsc == MARK_ESCAPE)
    {
        s_Phase = cjIn < PHASE_COUNT ? cjIn : 0;
        return 1;
    }
    Enter(26);
    Result = s_Original.DrvEscape(dhpdev, pso, iEsc, cjIn, pvIn, cjOut, pvOut);
    Leave();
    Fields[0] = iEsc;
    Fields[1] = cjIn;
    Fields[2] = cjOut;
    Fields[3] = SurfaceKey(pso);
    Fields[4] = Result;
    AddShape(26, Fields, 5);
    if (iEsc != ESCAPE_UNSUPPORTED_QUERY)
        AddEvent(26, iEsc, cjIn, Result);
    if ((s_Probe & PROBE_BIT_ESCAPE) && iEsc == ESCAPE_GET_SCREEN_ROTATION && pvOut && cjOut)
        AddDump("probe escape 6302 output", pvOut, cjOut);
    return Result;
}

static BOOL TraceGradientFill(SURFOBJ *pso, CLIPOBJ *pco, XLATEOBJ *pxlo, TRIVERTEX *pVertex, ULONG nVertex, PVOID pMesh, ULONG nMesh,
                              RECTL *prclExtents, POINTL *pptlDitherOrg, ULONG ulMode)
{
    BOOL Result;
    ULONG Fields[6];

    Fields[0] = SurfaceKey(pso);
    Fields[1] = ClipKey(pco);
    Fields[2] = XlateFlags(pxlo);
    Fields[3] = ulMode;
    Fields[4] = nVertex;
    Fields[5] = nMesh;
    AddShape(27, Fields, 6);
    Enter(27);
    Result = s_Original.DrvGradientFill(pso, pco, pxlo, pVertex, nVertex, pMesh, nMesh, prclExtents, pptlDitherOrg, ulMode);
    Leave();
    return Result;
}

static ULONG TraceExtra(ULONG Index, const ULONG *pArguments)
{
    if (!s_ExtraCalls[Index])
        memcpy(s_ExtraFirstArguments[Index], pArguments, sizeof(s_ExtraFirstArguments[Index]));
    InterlockedIncrement((LONG *)&s_ExtraCalls[Index]);
    AddEvent(EXTRA_SLOT_BASE + Index, pArguments[0], pArguments[1], pArguments[2]);
    return 0;
}

#define EXTRA_WRAPPER(Index) \
    static ULONG TraceExtra##Index(ULONG a0, ULONG a1, ULONG a2, ULONG a3, ULONG a4, ULONG a5, ULONG a6, ULONG a7, ULONG a8, ULONG a9, \
                                   ULONG a10, ULONG a11, ULONG a12, ULONG a13) \
    { \
        ULONG Arguments[EXTRA_ARGUMENTS] = { a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13 }; \
        ULONG Result; \
        TraceExtra(Index, Arguments); \
        Enter(EXTRA_SLOT_BASE + Index); \
        Result = s_OriginalExtra[Index](a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13); \
        Leave(); \
        return Result; \
    }

EXTRA_WRAPPER(0)
EXTRA_WRAPPER(1)
EXTRA_WRAPPER(2)

static const EXTRA_FUNCTION s_ExtraWrappers[EXTRA_SLOT_COUNT] = { TraceExtra0, TraceExtra1, TraceExtra2 };

static void TraceExtraCallback(ULONG Index, const ULONG *pArguments)
{
    if (!s_ExtraCallbackCalls[Index])
        memcpy(s_ExtraCallbackFirstArguments[Index], pArguments, sizeof(s_ExtraCallbackFirstArguments[Index]));
    InterlockedIncrement((LONG *)&s_ExtraCallbackCalls[Index]);
    if (s_ExtraCallbackCalls[Index] <= 4)
        AddEvent(EXTRA_CALLBACK_BASE + Index, pArguments[0], pArguments[1], pArguments[2]);
}

#define EXTRA_CALLBACK_WRAPPER(Index) \
    static ULONG TraceExtraCallback##Index(ULONG a0, ULONG a1, ULONG a2, ULONG a3, ULONG a4, ULONG a5, ULONG a6, ULONG a7, ULONG a8, \
                                           ULONG a9, ULONG a10, ULONG a11, ULONG a12, ULONG a13) \
    { \
        ULONG Arguments[EXTRA_ARGUMENTS] = { a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13 }; \
        TraceExtraCallback(Index, Arguments); \
        return s_OriginalExtraCallbacks[Index](a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13); \
    }

EXTRA_CALLBACK_WRAPPER(0)
EXTRA_CALLBACK_WRAPPER(1)
EXTRA_CALLBACK_WRAPPER(2)

static const EXTRA_FUNCTION s_ExtraCallbackWrappers[EXTRA_CALLBACK_COUNT] = { TraceExtraCallback0, TraceExtraCallback1, TraceExtraCallback2 };

static PVOID TraceAllocRbrush(BRUSHOBJ *pbo, ULONG cj)
{
    AddShape(CALLBACK_BASE + 0, &cj, 1);
    return s_OriginalCallbacks.BRUSHOBJ_pvAllocRbrush(pbo, cj);
}

static PVOID TraceGetRbrush(BRUSHOBJ *pbo)
{
    AddShape(CALLBACK_BASE + 1, NULL, 0);
    return s_OriginalCallbacks.BRUSHOBJ_pvGetRbrush(pbo);
}

static ULONG TraceClipEnumStart(CLIPOBJ *pco, BOOL bAll, ULONG iType, ULONG iDirection, ULONG cLimit)
{
    ULONG Fields[4];

    Fields[0] = (ULONG)bAll;
    Fields[1] = iType;
    Fields[2] = iDirection;
    Fields[3] = cLimit;
    AddShape(CALLBACK_BASE + 2, Fields, 4);
    return s_OriginalCallbacks.CLIPOBJ_cEnumStart(pco, bAll, iType, iDirection, cLimit);
}

static BOOL TraceClipEnum(CLIPOBJ *pco, ULONG cj, ULONG *pul)
{
    AddShape(CALLBACK_BASE + 3, NULL, 0);
    return s_OriginalCallbacks.CLIPOBJ_bEnum(pco, cj, pul);
}

static ULONG TracePaletteColors(PALOBJ *ppalo, ULONG iStart, ULONG cColors, ULONG *pulColors)
{
    AddShape(CALLBACK_BASE + 4, NULL, 0);
    return s_OriginalCallbacks.PALOBJ_cGetColors(ppalo, iStart, cColors, pulColors);
}

static VOID TracePathEnumStart(PATHOBJ *ppo)
{
    AddShape(CALLBACK_BASE + 5, NULL, 0);
    s_OriginalCallbacks.PATHOBJ_vEnumStart(ppo);
}

static BOOL TracePathEnum(PATHOBJ *ppo, PATHDATA *ppd)
{
    BOOL Result = s_OriginalCallbacks.PATHOBJ_bEnum(ppo, ppd);
    ULONG Field = ppd ? ppd->flags : NO_OBJECT;

    AddShape(CALLBACK_BASE + 6, &Field, 1);
    return Result;
}

static VOID TracePathBounds(PATHOBJ *ppo, PRECTFX prectfx)
{
    AddShape(CALLBACK_BASE + 7, NULL, 0);
    s_OriginalCallbacks.PATHOBJ_vGetBounds(ppo, prectfx);
}

static ULONG TraceXlatePalette(XLATEOBJ *pxlo, ULONG iPal, ULONG cPal, ULONG *pPal)
{
    AddShape(CALLBACK_BASE + 8, &iPal, 1);
    return s_OriginalCallbacks.XLATEOBJ_cGetPalette(pxlo, iPal, cPal, pPal);
}

static HSURF TraceCreateDeviceSurface(DHSURF dhsurf, SIZEL sizl, ULONG iFormatCompat)
{
    HSURF Result;
    ULONG Words[5];

    AddShape(CALLBACK_BASE + 9, &iFormatCompat, 1);
    Result = s_OriginalCallbacks.EngCreateDeviceSurface(dhsurf, sizl, iFormatCompat);
    s_PrimaryDeviceSurface = dhsurf;
    Words[0] = (ULONG)dhsurf;
    Words[1] = (ULONG)sizl.cx;
    Words[2] = (ULONG)sizl.cy;
    Words[3] = iFormatCompat;
    Words[4] = (ULONG)Result;
    AddDump("EngCreateDeviceSurface dhsurf cx cy format returned", Words, sizeof(Words));
    return Result;
}

static BOOL TraceDeleteSurface(HSURF hsurf)
{
    AddShape(CALLBACK_BASE + 10, NULL, 0);
    return s_OriginalCallbacks.EngDeleteSurface(hsurf);
}

static HBITMAP TraceEngCreateDeviceBitmap(DHSURF dhsurf, SIZEL sizl, ULONG iFormatCompat)
{
    AddShape(CALLBACK_BASE + 11, &iFormatCompat, 1);
    return s_OriginalCallbacks.EngCreateDeviceBitmap(dhsurf, sizl, iFormatCompat);
}

static HPALETTE TraceCreatePalette(ULONG iMode, ULONG cColors, PULONG pulColors, FLONG flRed, FLONG flGreen, FLONG flBlue)
{
    ULONG Fields[5];

    Fields[0] = iMode;
    Fields[1] = cColors;
    Fields[2] = flRed;
    Fields[3] = flGreen;
    Fields[4] = flBlue;
    AddShape(CALLBACK_BASE + 12, Fields, 5);
    return s_OriginalCallbacks.EngCreatePalette(iMode, cColors, pulColors, flRed, flGreen, flBlue);
}

static void Append(REPORT *pReport, const char *pText)
{
    while (*pText && pReport->Length < REPORT_BYTES - 1)
        pReport->pText[pReport->Length++] = *pText++;
}

static void AppendHex(REPORT *pReport, ULONG Value)
{
    static const char Digits[] = "0123456789abcdef";
    char Text[11];
    int Index;

    Text[0] = '0';
    Text[1] = 'x';
    for (Index = 0; Index < 8; Index++)
        Text[2 + Index] = Digits[(Value >> (28 - Index * 4)) & 0xf];
    Text[10] = 0;
    Append(pReport, Text);
}

static void AppendDecimal(REPORT *pReport, ULONG Value)
{
    char Text[11];
    int Index = 10;

    Text[Index] = 0;
    do
    {
        Text[--Index] = (char)('0' + Value % 10);
        Value /= 10;
    } while (Value);
    Append(pReport, &Text[Index]);
}

static void AppendLabel(REPORT *pReport, const char *pName, ULONG Value)
{
    Append(pReport, pName);
    Append(pReport, " ");
    AppendDecimal(pReport, Value);
    Append(pReport, "\r\n");
}

static const char *SlotName(ULONG Slot)
{
    static const char *const ExtraNames[EXTRA_SLOT_COUNT] = { "extra slot 28", "extra slot 29", "extra slot 30" };
    static const char *const ExtraCallbackNames[EXTRA_CALLBACK_COUNT] = { "extra callback 13", "extra callback 14", "extra callback 15" };

    if (Slot < SLOT_COUNT)
        return s_SlotNames[Slot];
    if (Slot >= EXTRA_SLOT_BASE && Slot < EXTRA_SLOT_BASE + EXTRA_SLOT_COUNT)
        return ExtraNames[Slot - EXTRA_SLOT_BASE];
    if (Slot >= EXTRA_CALLBACK_BASE && Slot < EXTRA_CALLBACK_BASE + EXTRA_CALLBACK_COUNT)
        return ExtraCallbackNames[Slot - EXTRA_CALLBACK_BASE];
    if (Slot >= CALLBACK_BASE && Slot < CALLBACK_BASE + CALLBACK_COUNT)
        return s_CallbackNames[Slot - CALLBACK_BASE];
    if (Slot >= PROBE_BASE && Slot < PROBE_BASE + PROBE_COUNT)
        return s_ProbeNames[Slot - PROBE_BASE];
    return "unknown";
}

static void AppendStatus(REPORT *pReport)
{
    Append(pReport, "ddi-trace stage ");
    AppendDecimal(pReport, s_Stage);
    Append(pReport, " last entered ");
    Append(pReport, s_LastEntered == NO_OBJECT ? "none" : SlotName(s_LastEntered));
    Append(pReport, " depth ");
    AppendDecimal(pReport, (ULONG)s_Depth);
    Append(pReport, "\r\n");
}

static void AppendShapes(REPORT *pReport, ULONG Slot)
{
    DWORD Index, Field;

    for (Index = 0; Index < SHAPE_CAPACITY; Index++)
    {
        const SHAPE *pShape = &s_Shapes[Index];

        if (!pShape->Count || pShape->Slot != Slot)
            continue;
        Append(pReport, "  ");
        AppendDecimal(pReport, pShape->Count);
        Append(pReport, " x");
        for (Field = 0; Field < pShape->FieldCount; Field++)
        {
            const char *pName = Slot < SLOT_COUNT ? s_FieldNames[Slot][Field] : NULL;

            if (Slot >= PROBE_BASE && Slot < PROBE_BASE + PROBE_COUNT)
                pName = s_ProbeFieldNames[Slot - PROBE_BASE][Field];

            Append(pReport, " ");
            Append(pReport, pName ? pName : "field");
            Append(pReport, "=");
            AppendHex(pReport, pShape->Fields[Field]);
        }
        Append(pReport, "\r\n");
    }
}

static void AppendPhases(REPORT *pReport)
{
    DWORD Phase, Slot;

    for (Phase = 0; Phase < PHASE_COUNT; Phase++)
    {
        BOOL bAny = FALSE;

        for (Slot = 0; Slot < COUNTED_SLOTS; Slot++)
        {
            if (!s_PhaseCalls[Phase][Slot])
                continue;
            if (!bAny)
            {
                Append(pReport, "phase ");
                AppendDecimal(pReport, Phase);
                Append(pReport, ":");
                bAny = TRUE;
            }
            Append(pReport, " ");
            Append(pReport, SlotName(Slot));
            Append(pReport, "=");
            AppendDecimal(pReport, s_PhaseCalls[Phase][Slot]);
        }
        if (bAny)
            Append(pReport, "\r\n");
    }
}

static void BuildReport(REPORT *pReport)
{
    DWORD Index, Word;

    pReport->Length = 0;
    AppendStatus(pReport);
    Append(pReport, "engine version ");
    AppendHex(pReport, s_EngineVersion);
    Append(pReport, " enable data bytes ");
    AppendDecimal(pReport, s_EnableDataSize);
    Append(pReport, " wrap mask ");
    AppendHex(pReport, s_WrapMask);
    Append(pReport, " wrap callbacks ");
    AppendDecimal(pReport, s_WrapCallbacks);
    Append(pReport, "\r\ncallback words ");
    AppendDecimal(pReport, s_CallbackWords);
    Append(pReport, "\r\n");
    AppendLabel(pReport, "HALInit calls", s_HalInitCalls);
    AppendLabel(pReport, "shapes", s_ShapeCount);
    AppendLabel(pReport, "shapes dropped", s_ShapeOverflow);
    for (Index = 0; Index < SLOT_COUNT; Index++)
    {
        Append(pReport, "Drv");
        Append(pReport, s_SlotNames[Index]);
        Append(pReport, (s_WrapMask >> Index) & 1 ? " calls " : " (not wrapped) calls ");
        AppendDecimal(pReport, s_Calls[Index]);
        Append(pReport, "\r\n");
        AppendShapes(pReport, Index);
    }
    for (Index = 0; Index < CALLBACK_COUNT; Index++)
    {
        Append(pReport, s_CallbackNames[Index]);
        Append(pReport, " calls ");
        AppendDecimal(pReport, s_CallbackCalls[Index]);
        Append(pReport, "\r\n");
        AppendShapes(pReport, CALLBACK_BASE + Index);
    }
    for (Index = 0; Index < EXTRA_SLOT_COUNT; Index++)
    {
        Append(pReport, SlotName(EXTRA_SLOT_BASE + Index));
        Append(pReport, " calls ");
        AppendDecimal(pReport, s_ExtraCalls[Index]);
        if (s_ExtraCalls[Index])
        {
            Append(pReport, " first arguments");
            for (Word = 0; Word < EXTRA_ARGUMENTS; Word++)
            {
                Append(pReport, " ");
                AppendHex(pReport, s_ExtraFirstArguments[Index][Word]);
            }
        }
        Append(pReport, "\r\n");
    }
    for (Index = 0; Index < EXTRA_CALLBACK_COUNT; Index++)
    {
        Append(pReport, SlotName(EXTRA_CALLBACK_BASE + Index));
        Append(pReport, " calls ");
        AppendDecimal(pReport, s_ExtraCallbackCalls[Index]);
        if (s_ExtraCallbackCalls[Index])
        {
            Append(pReport, " first arguments");
            for (Word = 0; Word < EXTRA_ARGUMENTS; Word++)
            {
                Append(pReport, " ");
                AppendHex(pReport, s_ExtraCallbackFirstArguments[Index][Word]);
            }
        }
        Append(pReport, "\r\n");
    }
    for (Index = 0; Index < PROBE_COUNT; Index++)
    {
        Append(pReport, s_ProbeNames[Index]);
        Append(pReport, " calls ");
        AppendDecimal(pReport, s_ProbeCalls[Index]);
        Append(pReport, "\r\n");
        AppendShapes(pReport, PROBE_BASE + Index);
    }
    for (Index = 0; Index < 256; Index++)
    {
        if (!s_MaskTableCounts[Index])
            continue;
        Append(pReport, "mask value ");
        AppendDecimal(pReport, Index);
        Append(pReport, " pixel ");
        AppendHex(pReport, s_MaskTable[Index]);
        Append(pReport, " count ");
        AppendDecimal(pReport, s_MaskTableCounts[Index]);
        Append(pReport, "\r\n");
    }
    Append(pReport, "events (tick slot values):\r\n");
    for (Index = s_EventCount > EVENT_CAPACITY ? s_EventCount - EVENT_CAPACITY : 0; Index < s_EventCount; Index++)
    {
        const EVENT *pEvent = &s_Events[Index % EVENT_CAPACITY];

        Append(pReport, "  ");
        AppendDecimal(pReport, pEvent->Tick);
        Append(pReport, " ");
        Append(pReport, SlotName(pEvent->Slot));
        Append(pReport, " ");
        AppendHex(pReport, pEvent->First);
        Append(pReport, " ");
        AppendHex(pReport, pEvent->Second);
        Append(pReport, " ");
        AppendHex(pReport, pEvent->Third);
        Append(pReport, "\r\n");
    }
    AppendPhases(pReport);
    for (Index = 0; Index < s_DumpCount; Index++)
    {
        Append(pReport, "dump ");
        Append(pReport, s_Dumps[Index].pName);
        Append(pReport, " words ");
        AppendDecimal(pReport, s_Dumps[Index].WordCount);
        Append(pReport, ":");
        for (Word = 0; Word < s_Dumps[Index].WordCount; Word++)
        {
            Append(pReport, Word % 8 ? " " : "\r\n ");
            AppendHex(pReport, s_Dumps[Index].Words[Word]);
        }
        Append(pReport, "\r\n");
    }
}

static void WriteReport(const REPORT *pReport)
{
    HANDLE hFile;
    DWORD Written;

    hFile = CreateFile(s_OutputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(hFile, pReport->pText, pReport->Length, &Written, NULL);
    CloseHandle(hFile);
}

static DWORD WINAPI WriterThread(LPVOID pParameter)
{
    REPORT Report;
    DWORD Delay = FIRST_WRITE_MILLISECONDS;

    Report.pText = (char *)VirtualAlloc(NULL, REPORT_BYTES, MEM_COMMIT, PAGE_READWRITE);
    if (!Report.pText)
        return 0;
    for (;;)
    {
        DWORD Attempt;
        BOOL bLocked = FALSE;

        Sleep(Delay);
        Delay = WRITE_INTERVAL_MILLISECONDS;
        if (GetFileAttributes(STORAGE_FOLDER) == 0xFFFFFFFF)
            continue;
        for (Attempt = 0; Attempt < LOCK_ATTEMPTS && !bLocked; Attempt++)
        {
            bLocked = TryEnterCriticalSection(&s_Lock);
            if (!bLocked)
                Sleep(10);
        }
        if (bLocked)
        {
            BuildReport(&Report);
            LeaveCriticalSection(&s_Lock);
        }
        else
        {
            Report.Length = 0;
            Append(&Report, "lock stuck\r\n");
            AppendStatus(&Report);
        }
        WriteReport(&Report);
    }
}

static DWORD ReadDword(HKEY hKey, LPCTSTR pName, DWORD Default)
{
    DWORD Value, Type, Size = sizeof(Value);

    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE *)&Value, &Size) == ERROR_SUCCESS && Type == REG_DWORD)
        return Value;
    return Default;
}

static void ReadString(HKEY hKey, LPCTSTR pName, TCHAR *pValue)
{
    TCHAR Buffer[MAX_PATH];
    DWORD Type, Size = sizeof(Buffer) - sizeof(TCHAR);

    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE *)Buffer, &Size) == ERROR_SUCCESS && Type == REG_SZ && Size)
    {
        Buffer[Size / sizeof(TCHAR)] = 0;
        lstrcpy(pValue, Buffer);
    }
}

static BOOL LoadOriginal(void)
{
    HKEY hKey;

    if (s_hOriginal)
        return TRUE;
    InitializeCriticalSection(&s_Lock);
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) == ERROR_SUCCESS)
    {
        ReadString(hKey, TEXT("Driver"), s_DriverName);
        ReadString(hKey, TEXT("Output"), s_OutputPath);
        s_WrapMask = ReadDword(hKey, TEXT("Wrap"), DEFAULT_WRAP_MASK);
        s_WrapCallbacks = ReadDword(hKey, TEXT("WrapCallbacks"), 0);
        s_Probe = ReadDword(hKey, TEXT("Probe"), 0);
        s_ClearExtraSlots = ReadDword(hKey, TEXT("ClearExtraSlots"), 0);
        RegCloseKey(hKey);
    }
    CloseHandle(CreateThread(NULL, 0, WriterThread, NULL, 0, NULL));
    s_Stage = 1;
    s_hOriginal = LoadLibrary(s_DriverName);
    if (!s_hOriginal)
        return FALSE;
    s_pHalInit = (HAL_INIT_FUNCTION)GetProcAddress(s_hOriginal, TEXT("HALInit"));
    return TRUE;
}

static void WrapCallbacks(PENGCALLBACKS pCallbacks)
{
    PENGCALLBACKS pCopy = (PENGCALLBACKS)s_CallbackCopy;
    PVOID *pWords = (PVOID *)pCallbacks;

    while (s_CallbackWords < CALLBACK_COPY_WORDS && pWords[s_CallbackWords])
        s_CallbackWords++;
    memcpy(s_CallbackCopy, pCallbacks, s_CallbackWords * sizeof(PVOID));
    if (!s_WrapCallbacks || s_CallbackWords < CALLBACK_COUNT)
        return;
    s_OriginalCallbacks = *pCallbacks;
    pCopy->BRUSHOBJ_pvAllocRbrush = TraceAllocRbrush;
    pCopy->BRUSHOBJ_pvGetRbrush = TraceGetRbrush;
    pCopy->CLIPOBJ_cEnumStart = TraceClipEnumStart;
    pCopy->CLIPOBJ_bEnum = TraceClipEnum;
    pCopy->PALOBJ_cGetColors = TracePaletteColors;
    pCopy->PATHOBJ_vEnumStart = TracePathEnumStart;
    pCopy->PATHOBJ_bEnum = TracePathEnum;
    pCopy->PATHOBJ_vGetBounds = TracePathBounds;
    pCopy->XLATEOBJ_cGetPalette = TraceXlatePalette;
    pCopy->EngCreateDeviceSurface = TraceCreateDeviceSurface;
    pCopy->EngDeleteSurface = TraceDeleteSurface;
    pCopy->EngCreateDeviceBitmap = TraceEngCreateDeviceBitmap;
    pCopy->EngCreatePalette = TraceCreatePalette;
    if (s_Probe & PROBE_BIT_EXTRA_CALLBACKS)
    {
        ULONG Extra;

        for (Extra = 0; Extra < EXTRA_CALLBACK_COUNT && CALLBACK_COUNT + Extra < s_CallbackWords; Extra++)
        {
            s_OriginalExtraCallbacks[Extra] = (EXTRA_FUNCTION)s_CallbackCopy[CALLBACK_COUNT + Extra];
            s_CallbackCopy[CALLBACK_COUNT + Extra] = (PVOID)s_ExtraCallbackWrappers[Extra];
        }
    }
}

#define WRAP(Slot, Member, Wrapper) \
    if ((s_WrapMask >> (Slot)) & 1 && pded->Member) \
        pded->Member = Wrapper

BOOL APIENTRY DrvEnableDriver(ULONG iEngineVersion, ULONG cj, DRVENABLEDATA *pded, PENGCALLBACKS pEngCallbacks)
{
    ENABLE_DRIVER_FUNCTION pEnable;
    PENGCALLBACKS pCallbacks = pEngCallbacks;

    if (!LoadOriginal())
        return FALSE;
    s_EngineVersion = iEngineVersion;
    s_EnableDataSize = cj;
    if (pEngCallbacks)
    {
        WrapCallbacks(pEngCallbacks);
        if (s_WrapCallbacks && s_CallbackWords >= CALLBACK_COUNT)
            pCallbacks = (PENGCALLBACKS)s_CallbackCopy;
    }
    pEnable = (ENABLE_DRIVER_FUNCTION)GetProcAddress(s_hOriginal, TEXT("DrvEnableDriver"));
    if (!pEnable || !pEnable(iEngineVersion, cj, pded, pCallbacks))
        return FALSE;
    s_Stage = 2;
    AddDump("DrvEnableDriver extra slots", (const BYTE *)pded + sizeof(DRVENABLEDATA), cj > sizeof(DRVENABLEDATA) ? cj - sizeof(DRVENABLEDATA) : 0);
    if (pEngCallbacks && s_CallbackWords > CALLBACK_COUNT)
        AddDump("ENGCALLBACKS extra entries", (const PVOID *)pEngCallbacks + CALLBACK_COUNT, (s_CallbackWords - CALLBACK_COUNT) * sizeof(PVOID));
    if (cj < sizeof(DRVENABLEDATA))
        return TRUE;
    s_Original = *pded;
    WRAP(0, DrvEnablePDEV, TraceEnablePDEV);
    WRAP(1, DrvDisablePDEV, TraceDisablePDEV);
    WRAP(2, DrvEnableSurface, TraceEnableSurface);
    WRAP(3, DrvDisableSurface, TraceDisableSurface);
    WRAP(4, DrvCreateDeviceBitmap, TraceCreateDeviceBitmap);
    WRAP(5, DrvDeleteDeviceBitmap, TraceDeleteDeviceBitmap);
    WRAP(6, DrvRealizeBrush, TraceRealizeBrush);
    WRAP(7, DrvStrokePath, TraceStrokePath);
    WRAP(8, DrvFillPath, TraceFillPath);
    WRAP(9, DrvPaint, TracePaint);
    WRAP(10, DrvBitBlt, TraceBitBlt);
    WRAP(11, DrvCopyBits, TraceCopyBits);
    WRAP(12, DrvAnyBlt, TraceAnyBlt);
    WRAP(13, DrvTransparentBlt, TraceTransparentBlt);
    WRAP(14, DrvSetPalette, TraceSetPalette);
    WRAP(15, DrvSetPointerShape, TraceSetPointerShape);
    WRAP(16, DrvMovePointer, TraceMovePointer);
    WRAP(17, DrvGetModes, TraceGetModes);
    WRAP(18, DrvRealizeColor, TraceRealizeColor);
    WRAP(19, DrvGetMasks, TraceGetMasks);
    WRAP(20, DrvUnrealizeColor, TraceUnrealizeColor);
    WRAP(21, DrvContrastControl, TraceContrastControl);
    WRAP(22, DrvPowerHandler, TracePowerHandler);
    WRAP(23, DrvEndDoc, TraceEndDoc);
    WRAP(24, DrvStartDoc, TraceStartDoc);
    WRAP(25, DrvStartPage, TraceStartPage);
    WRAP(26, DrvEscape, TraceEscape);
    WRAP(27, DrvGradientFill, TraceGradientFill);
    {
        EXTRA_FUNCTION *pExtra = (EXTRA_FUNCTION *)((BYTE *)pded + sizeof(DRVENABLEDATA));
        ULONG Extra, ExtraCount = (cj - sizeof(DRVENABLEDATA)) / sizeof(EXTRA_FUNCTION);

        for (Extra = 0; Extra < ExtraCount && Extra < EXTRA_SLOT_COUNT; Extra++)
        {
            if (s_ClearExtraSlots)
            {
                pExtra[Extra] = NULL;
                continue;
            }
            if (!pExtra[Extra] || !((s_WrapMask >> (SLOT_COUNT + Extra)) & 1))
                continue;
            s_OriginalExtra[Extra] = pExtra[Extra];
            pExtra[Extra] = s_ExtraWrappers[Extra];
        }
    }
    return TRUE;
}

ULONG HALInit(ULONG a0, ULONG a1, ULONG a2, ULONG a3)
{
    if (!LoadOriginal() || !s_pHalInit)
        return FALSE;
    InterlockedIncrement((LONG *)&s_HalInitCalls);
    return s_pHalInit(a0, a1, a2, a3);
}
