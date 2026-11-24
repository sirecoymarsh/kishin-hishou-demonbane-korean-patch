// Balanced DUEL routes preserve the original damage row. V2 separates manual
// and automatic context; only scene-specific AI definition paths change.
static void LoadDuelAiRoutes()
{
    aiRouteCount = 0;
    wchar_t path[MAX_PATH], pack[MAX_PATH];
    swprintf_s(path,L"%s\\duel_ai_routes.tsv",abDir);
    swprintf_s(pack,L"%s\\dmpack508_dueltiers.dat",gameDir);
    if (GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES) { Log("DUEL AI profiles: no tier pack, native AI retained"); return; }
    FILE* f=NULL;
    if (_wfopen_s(&f,path,L"rb") || !f) { Log("DUEL AI profiles: no routes, native AI retained"); return; }
    char line[256]; bool valid=true; int version=1;
    while(fgets(line,sizeof line,f))
    {
        line[strcspn(line,"\r\n")]=0;
        if(strncmp(line,"# duel-ai-routes-v",17)==0)
        {
            if(aiRouteCount || strcmp(line,"# duel-ai-routes-v2")) { valid=false; break; }
            version=2; continue;
        }
        if(line[0]=='#' || !line[0]) continue;
        DuelAiRoute r={}; r.control=1; // V1 pilot tables were automatic only.
        int parsed=version==2 ? sscanf_s(line,"%d\t%d\t%d\t%d\t%d",&r.tier,&r.control,&r.source,&r.loaded,&r.destination)
                              : sscanf_s(line,"%d\t%d\t%d\t%d",&r.tier,&r.source,&r.loaded,&r.destination);
        if(parsed!=(version==2 ? 5 : 4) || r.control<0 || r.control>1 ||
           r.tier<0 || r.tier>2 || r.source<58 || r.source>=1000 || r.loaded<58 || r.loaded>=2000 ||
           r.destination<3000 || r.destination>=10000 || aiRouteCount>=static_cast<int>(_countof(aiRoutes))) { valid=false; break; }
        for(int i=0;i<aiRouteCount;++i)
            if(aiRoutes[i].destination==r.destination || (aiRoutes[i].tier==r.tier && aiRoutes[i].control==r.control && aiRoutes[i].source==r.source)) valid=false;
        if(!valid) break;
        aiRoutes[aiRouteCount++]=r;
    }
    fclose(f);
    if(!valid) { aiRouteCount=0; Log("DUEL AI profiles: invalid route table, all disabled"); }
    else Log("DUEL AI profiles: %d isolated routes loaded (table v%d)",aiRouteCount,version);
}

static void ApplyDuelAiProfile(BYTE* obj)
{
    if(!duelBalance || balScene<58 || balScene>=1000) return;
    const int control=duelAutoFight ? 1 : 0;
    const LONG loaded=*(LONG*)(obj+0x18);
    for(int i=0;i<aiRouteCount;++i)
    {
        const DuelAiRoute& r=aiRoutes[i];
        if(r.tier!=duelDifficulty || r.control!=control || r.source!=balScene) continue;
        if(r.loaded!=loaded) { Log("DUEL AI profile skipped: source %ld expected %d, loaded %ld",balScene,r.loaded,loaded); return; }
        *(LONG*)(obj+0x18)=r.destination;
        Log("DUEL AI profile: tier %ld control %d source %ld balanced %ld -> %d; combat rank Normal, damage row unchanged",duelDifficulty,control,balScene,loaded,r.destination);
        return;
    }
    Log("DUEL AI profile: tier %ld control %d source %ld not configured, native AI retained",duelDifficulty,control,balScene);
}
