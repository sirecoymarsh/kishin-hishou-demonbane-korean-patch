// Only MM<->GM scenes with an actual MM AI actor.
// 11-05 18:5x user 「쓰게 해 줘」: with balance off too (the fixed scenes differ only in Lord Byakhee's `jump MREQ(1)` line;
// the caller passes the loaded scene as the source when balance is off).
static int MmOtherDestination(bool balanced,int source,int loaded)
{
    (void)balanced;
    switch(source) {
    case 512: return loaded==512 ? 5910 : -1;
    case 513: return loaded==513 ? 5940 : -1;
    case 483: return loaded==483 ? 5941 : -1;
    case 63: return loaded==63 ? 5942 : -1;
    default:return -1;
    }
}
