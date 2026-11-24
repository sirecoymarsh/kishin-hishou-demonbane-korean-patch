// Explicit MM/QM scene allowlist. Never infer another opponent or mirror it globally.
// 11-05 18:5x user (balance off, Nero vs Lord Byakhee 627: 「균형 전투를 끄니까 로드비야키가 네로와의 전투에서 갑자기 목표 못찾고
// 정지해있는데」 → 「쓰게 해 줘」): the fixed scenes are used with balance off too. They are the original scenes with the C12
// definitions only (Lord Byakhee's `jump MREQ(1)` back on, Nero's two stand-still M_SHOT jumps out) - no damage / HP numbers
// (those stay with the balance table, applied only while balance is on). With balance off the source is the loaded scene
// itself. 522 with balance off keeps its own repair (native scene + MM_PLAYER_NORMAL in mm_qm_anchor_fix.h).
static int mmQmRoutedTo = -1;                // the fixed scene this fight was sent to, else -1 (InstallMmAimFix keys on it)
static int MmQmDestination(bool balanced,int source,int loaded)
{
    if (source!=loaded) return -1;
    switch (source) {
    case 522: return balanced ? 5923 : -1; // automatic, MM left
    case 627: return 5924; // automatic, MM right
    case 102: return 5925; // manual MM left; only Nero owns a native AI
    case 207: return 5926; // manual Nero left; only MM owns a native AI
    default: return -1;
    }
}
