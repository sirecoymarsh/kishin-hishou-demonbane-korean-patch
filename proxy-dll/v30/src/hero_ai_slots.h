// The five hero AIs (11-01 merge of the AM/A2/EM/HM/F2 candidates) share three native tick slots:
// LOAD e80c8 (HM, A2, F2, and the MM-QM / native-command hooks), DEMONTWOSWORD e7ac8 (AM), AION e81c8 (EM).
// Each Apply puts back only its own tick, and every installer requires the native tick in the slot.
// When the battle module stays loaded, a tick left by the previous fight would disable the next owner.
// Put all of them back before any installer runs; a freshly loaded module has nothing to restore.
#pragma once
static void RestoreHeroAiSlots(HMODULE module) {
    BYTE* b=(BYTE*)module;
    if(!b)return;
    if(hmModule==b && *(void**)(b+0xe80c8)==(void*)HmTick && hmPreviousTick)
        MmSwapSlot((void**)(b+0xe80c8),(void*)HmTick,(void*)hmPreviousTick);
    if(a2Module==b && *(void**)(b+0xe80c8)==(void*)A2Tick && a2PreviousTick)
        MmSwapSlot((void**)(b+0xe80c8),(void*)A2Tick,(void*)a2PreviousTick);
    if(f2Module==b && *(void**)(b+0xe80c8)==(void*)F2Tick && f2Previous)
        MmSwapSlot((void**)(b+0xe80c8),(void*)F2Tick,(void*)f2Previous);
    if(amModule==b && *(void**)(b+0xe7ac8)==(void*)AmTick && amPreviousTick)
        MmSwapSlot((void**)(b+0xe7ac8),(void*)AmTick,(void*)amPreviousTick);
    if(emOwnModule==b && *(void**)(b+0xe81c8)==(void*)EmOwnTick && emOwnPrevious)
        MmSwapSlot((void**)(b+0xe81c8),(void*)EmOwnTick,(void*)emOwnPrevious);
}
