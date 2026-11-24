// Exact R8/R9 predicate dependency. Installed Aion source is unchanged.
#pragma once
static bool AnActualIdle(const char* name)
{
    return name && (!_stricmp(name,"STOP") || !_stricmp(name,"STOP_A") ||
        !_stricmp(name,"STOP_G") || !_stricmp(name,"STOP_X") || !_stricmp(name,"STOP_X2"));
}
